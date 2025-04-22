#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>

#if GTK_MAJOR_VERSION == 3
// gtk_file_chooser_set_use_native() exists only in GTK3
#define MAYBE_DISABLE_NATIVE_FILECHOOSER(dlg, flag) \
    gtk_file_chooser_set_use_native(GTK_FILE_CHOOSER(dlg), flag)
#else
// on GTK4 it's a no-op
#define MAYBE_DISABLE_NATIVE_FILECHOOSER(dlg, flag) /* nothing */
#endif

// ===========================
// Data Model and Helper Types
// ===========================

typedef struct {
    char *first_name;
    char *last_name;
    char *gender;
    char *elementary_school;
    char *bg_gutachten;
} Student;

typedef struct {
    char *student_a;
    char *student_b;
} Rule;

// Union-Find implementation for grouping students
typedef struct {
    int *parent;
    int size;
} UnionFind;

// Function prototypes
static void load_students(const char *file_path, Student **students, int *num_students);
static void distribute_students_optimized(Student *students, int num_students, int num_classes, Student ***classes, int **class_sizes);
static void distribute_students_with_rules(Student *students, int num_students, Rule *rules, int num_rules, 
                                        int num_classes, Student ***classes, int **class_sizes);
static char *compute_stats(Student *class_students, int num_students);
static double compute_cost(Student *class_list, int class_size, Student *s);
static double compute_group_cost(Student *class_list, int class_size, Student *group, int group_size);
static void shuffle_students(Student *students, int num_students);
static void open_add_rule_dialog(GtkWindow *parent, Student *students, int num_students, 
                               GArray *rules, GtkWidget *rule_textview, GtkNotebook *notebook,
                               void (*update_tabs_callback)(GtkNotebook*, Student*, int, GArray*, int));
static void update_rule_textview(GtkTextView *textview, GArray *rules);
static void update_tabs(GtkNotebook *notebook, Student *students, int num_students, GArray *rules, int num_classes);
static GtkWidget *create_student_treeview(Student *students, int num_students);
static bool str_equal_ignore_case(const char *s1, const char *s2);
static char *str_trim(char *str);
static char *str_dup(const char *str);
static bool str_is_empty(const char *str);
static void free_students(Student *students, int num_students);
static void union_find_init(UnionFind *uf, int size);
static int union_find_find(UnionFind *uf, int i);
static void union_find_union(UnionFind *uf, int i, int j);
static void union_find_free(UnionFind *uf);
static void show_error_dialog(GtkWindow *parent, const char *message);
static bool str_equal_case(const char *s1, const char *s2);

// Global variables to pass to callback functions
Student *g_students = NULL;
int g_num_students = 0;
int g_num_classes = 5;
GArray *g_rules = NULL;

// ===========================
// Memory Management Helpers
// ===========================

static char *str_dup(const char *str) {
    if (str == NULL) return NULL;
    
    char *result = malloc(strlen(str) + 1);
    if (result != NULL) {
        strcpy(result, str);
    }
    return result;
}

static void free_students(Student *students, int num_students) {
    for (int i = 0; i < num_students; i++) {
        free(students[i].first_name);
        free(students[i].last_name);
        free(students[i].gender);
        free(students[i].elementary_school);
        free(students[i].bg_gutachten);
    }
    free(students);
}

// ===========================
// String Utilities
// ===========================

static bool str_equal_ignore_case(const char *s1, const char *s2) {
    if (s1 == NULL || s2 == NULL) return false;
    
    while (*s1 && *s2) {
        if (tolower((unsigned char)*s1) != tolower((unsigned char)*s2)) {
            return false;
        }
        s1++;
        s2++;
    }
    return *s1 == *s2; // Both strings ended at the same time
}

static char *str_trim(char *str) {
    if (str == NULL) return NULL;
    
    // Trim leading spaces
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    
    if (*start == 0) {  // All spaces
        *str = 0;
        return str;
    }
    
    // Trim trailing spaces
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = 0;
    
    // If the original string was not modified, return it
    if (start == str) return str;
    
    // Otherwise, move the trimmed string to the beginning
    memmove(str, start, strlen(start) + 1);
    return str;
}

static bool str_is_empty(const char *str) {
    return str == NULL || *str == '\0';
}

static bool str_equal_case(const char *s1, const char *s2) {
    if (s1 == NULL || s2 == NULL) return false;
    return strcmp(s1, s2) == 0;
}

// ===========================
// Union-Find Implementation
// ===========================

static void union_find_init(UnionFind *uf, int size) {
    uf->size = size;
    uf->parent = (int*)malloc(size * sizeof(int));
    for (int i = 0; i < size; i++) {
        uf->parent[i] = i;
    }
}

static int union_find_find(UnionFind *uf, int i) {
    if (uf->parent[i] != i) {
        uf->parent[i] = union_find_find(uf, uf->parent[i]);
    }
    return uf->parent[i];
}

static void union_find_union(UnionFind *uf, int i, int j) {
    int root_i = union_find_find(uf, i);
    int root_j = union_find_find(uf, j);
    if (root_i != root_j) {
        uf->parent[root_j] = root_i;
    }
}

static void union_find_free(UnionFind *uf) {
    free(uf->parent);
    uf->parent = NULL;
    uf->size = 0;
}

// ===========================
// CSV Loading
// ===========================

static void load_students(const char *file_path, Student **students, int *num_students) {
    FILE *fp = fopen(file_path, "r");
    if (!fp) {
        fprintf(stderr, "Could not open file: %s\n", file_path);
        *students = NULL;
        *num_students = 0;
        return;
    }
    
    // Count lines to allocate memory
    int line_count = 0;
    char c;
    bool in_line = false;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '\n') {
            line_count++;
            in_line = false;
        } else if (!in_line) {
            in_line = true;
        }
    }
    if (in_line) line_count++; // Count the last line if it doesn't end with newline
    
    fprintf(stderr, "Total lines in file: %d\n", line_count);
    
    if (line_count <= 1) {
        fclose(fp);
        *students = NULL;
        *num_students = 0;
        return; // Just header or empty file
    }
    
    // Rewind file to start
    rewind(fp);
    
    // Read header
    char header_line[1024];
    if (fgets(header_line, sizeof(header_line), fp) == NULL) {
        fclose(fp);
        *students = NULL;
        *num_students = 0;
        return;
    }
    
    fprintf(stderr, "Header line: %s\n", header_line);
    
    // Find column indices
    char *headers[256]; // Maximum number of columns
    int header_count = 0;
    
    // Parse header line manually to handle empty fields
    char *start = header_line;
    char *end;
    while ((end = strchr(start, ',')) != NULL) {
        *end = '\0';
        headers[header_count] = str_dup(start);
        str_trim(headers[header_count]);
        fprintf(stderr, "Header %d: %s\n", header_count, headers[header_count]);
        header_count++;
        start = end + 1;
    }
    // Add the last field
    headers[header_count] = str_dup(start);
    str_trim(headers[header_count]);
    fprintf(stderr, "Header %d: %s\n", header_count, headers[header_count]);
    header_count++;
    
    int col_vorname = -1, col_nachname = -1, col_gender = -1;
    int col_grundschule = -1, col_bg = -1;
    
    for (int i = 0; i < header_count; i++) {
        if (str_equal_ignore_case(headers[i], "Vorname")) col_vorname = i;
        else if (str_equal_ignore_case(headers[i], "Nachname")) col_nachname = i;
        else if (str_equal_case(headers[i], "m/w")) col_gender = i;
        else if (str_equal_case(headers[i], "Grundschule")) col_grundschule = i;
        else if (str_equal_case(headers[i], "BG Gutachten")) col_bg = i;
    }
    
    fprintf(stderr, "Column indices: Vorname=%d, Nachname=%d, m/w=%d, Grundschule=%d, BG Gutachten=%d\n",
            col_vorname, col_nachname, col_gender, col_grundschule, col_bg);
    
    // Free header strings
    for (int i = 0; i < header_count; i++) {
        free(headers[i]);
    }
    
    if (col_vorname == -1 || col_nachname == -1 || col_gender == -1 || 
        col_grundschule == -1 || col_bg == -1) {
        fprintf(stderr, "Error: Required columns not found in CSV file\n");
        fclose(fp);
        *students = NULL;
        *num_students = 0;
        return; // Required columns not found
    }
    
    // Allocate memory for students
    *num_students = line_count - 1; // Subtract header line
    *students = (Student*)malloc((*num_students) * sizeof(Student));
    if (*students == NULL) {
        fclose(fp);
        *num_students = 0;
        return;
    }
    
    fprintf(stderr, "Allocated memory for %d students\n", *num_students);
    
    // Read student records
    char line[1024];
    int student_index = 0;
    
    while (fgets(line, sizeof(line), fp) != NULL && student_index < *num_students) {
        // Split line by commas manually to handle empty fields
        char *fields[256]; // Maximum number of fields
        int field_count = 0;
        
        char *start = line;
        char *end;
        while ((end = strchr(start, ',')) != NULL) {
            *end = '\0';
            fields[field_count] = str_dup(start);
            str_trim(fields[field_count]);
            field_count++;
            start = end + 1;
        }
        // Add the last field
        fields[field_count] = str_dup(start);
        str_trim(fields[field_count]);
        field_count++;
        
        fprintf(stderr, "Line %d: %d fields\n", student_index + 1, field_count);
        
        if (field_count >= header_count) {
            // Assign fields to student structure
            Student *s = &((*students)[student_index]);
            s->first_name = str_dup(col_vorname < field_count ? fields[col_vorname] : "");
            s->last_name = str_dup(col_nachname < field_count ? fields[col_nachname] : "");
            s->gender = str_dup(col_gender < field_count ? fields[col_gender] : "");
            s->elementary_school = str_dup(col_grundschule < field_count ? fields[col_grundschule] : "");
            s->bg_gutachten = str_dup(col_bg < field_count ? fields[col_bg] : "");
            
            fprintf(stderr, "Loaded student %d: %s %s\n", student_index + 1, s->first_name, s->last_name);
            student_index++;
        } else {
            fprintf(stderr, "Warning: Line %d has fewer fields than expected\n", student_index + 1);
        }
        
        // Free field strings
        for (int i = 0; i < field_count; i++) {
            free(fields[i]);
        }
    }
    
    fclose(fp);
    
    // Update actual number of students loaded
    *num_students = student_index;
    fprintf(stderr, "Successfully loaded %d students\n", *num_students);
}

// ===========================
// Distribution and Statistics
// ===========================

static void shuffle_students(Student *students, int num_students) {
    srand((unsigned int)time(NULL));
    for (int i = num_students - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        // Swap students[i] and students[j]
        Student temp = students[i];
        students[i] = students[j];
        students[j] = temp;
    }
}

static void distribute_students_optimized(Student *students, int num_students, int num_classes, 
                                       Student ***classes, int **class_sizes) {
    // Allocate memory for classes
    *classes = (Student**)malloc(num_classes * sizeof(Student*));
    *class_sizes = (int*)calloc(num_classes, sizeof(int));
    
    for (int i = 0; i < num_classes; i++) {
        (*classes)[i] = (Student*)malloc(num_students * sizeof(Student)); // Max possible size
    }
    
    // Make a copy of students array to shuffle
    Student *students_copy = (Student*)malloc(num_students * sizeof(Student));
    memcpy(students_copy, students, num_students * sizeof(Student));
    
    // Shuffle students
    shuffle_students(students_copy, num_students);
    
    // Distribute students
    for (int i = 0; i < num_students; i++) {
        // Find class with minimum size
        int min_index = 0;
        int min_size = (*class_sizes)[0];
        
        for (int j = 1; j < num_classes; j++) {
            if ((*class_sizes)[j] < min_size) {
                min_size = (*class_sizes)[j];
                min_index = j;
            }
        }
        
        // Add student to class
        (*classes)[min_index][(*class_sizes)[min_index]] = students_copy[i];
        (*class_sizes)[min_index]++;
    }
    
    free(students_copy);
}

static double compute_cost(Student *class_list, int class_size, Student *s) {
    int count_grundschule = 0;
    int count_gender = 0;
    int count_bg = 0;
    
    for (int i = 0; i < class_size; i++) {
        Student *stu = &class_list[i];
        
        // Check elementary school
        char *gs1 = !str_is_empty(s->elementary_school) ? s->elementary_school : "Unknown";
        char *gs2 = !str_is_empty(stu->elementary_school) ? stu->elementary_school : "Unknown";
        
        if (str_equal_ignore_case(gs1, gs2)) {
            count_grundschule++;
        }
        
        // Check gender
        if (!str_is_empty(s->gender) && !str_is_empty(stu->gender) && 
            str_equal_ignore_case(s->gender, stu->gender)) {
            count_gender++;
        }
        
        // Check bg_gutachten
        char *bp1 = !str_is_empty(s->bg_gutachten) ? s->bg_gutachten : "Unknown";
        char *bp2 = !str_is_empty(stu->bg_gutachten) ? stu->bg_gutachten : "Unknown";
        
        if (str_equal_ignore_case(bp1, bp2)) {
            count_bg++;
        }
    }
    
    return 3.0 * count_grundschule + 2.0 * count_gender + 1.0 * count_bg;
}

static double compute_group_cost(Student *class_list, int class_size, Student *group, int group_size) {
    double total_cost = 0.0;
    for (int i = 0; i < group_size; i++) {
        total_cost += compute_cost(class_list, class_size, &group[i]);
    }
    return total_cost;
}

static void distribute_students_with_rules(Student *students, int num_students, Rule *rules, int num_rules, 
                                        int num_classes, Student ***classes, int **class_sizes) {
    // Create map from name to index
    char **name_to_index_keys = (char**)malloc(num_students * sizeof(char*));
    int *name_to_index_values = (int*)malloc(num_students * sizeof(int));
    int name_to_index_size = 0;
    
    for (int i = 0; i < num_students; i++) {
        char full_name[1024];
        sprintf(full_name, "%s %s", students[i].first_name, students[i].last_name);
        name_to_index_keys[name_to_index_size] = str_dup(full_name);
        name_to_index_values[name_to_index_size] = i;
        name_to_index_size++;
    }
    
    // Initialize union-find data structure
    UnionFind uf;
    union_find_init(&uf, num_students);
    
    // Process rules
    for (int i = 0; i < num_rules; i++) {
        int idx_a = -1, idx_b = -1;
        
        // Look up student indices
        for (int j = 0; j < name_to_index_size; j++) {
            if (strcmp(name_to_index_keys[j], rules[i].student_a) == 0) {
                idx_a = name_to_index_values[j];
            }
            if (strcmp(name_to_index_keys[j], rules[i].student_b) == 0) {
                idx_b = name_to_index_values[j];
            }
        }
        
        if (idx_a != -1 && idx_b != -1) {
            union_find_union(&uf, idx_a, idx_b);
        }
    }
    
    // Group students by their root in the union-find structure
    typedef struct {
        int root;
        Student *students;
        int size;
        int capacity;
    } StudentGroup;
    
    StudentGroup *groups = (StudentGroup*)malloc(num_students * sizeof(StudentGroup)); // Max possible number of groups
    int num_groups = 0;
    
    for (int i = 0; i < num_students; i++) {
        int root = union_find_find(&uf, i);
        
        // Find if we already have this root in our groups
        int group_idx = -1;
        for (int j = 0; j < num_groups; j++) {
            if (groups[j].root == root) {
                group_idx = j;
                break;
            }
        }
        
        if (group_idx == -1) {
            // Create new group
            groups[num_groups].root = root;
            groups[num_groups].capacity = 10; // Initial capacity
            groups[num_groups].students = (Student*)malloc(groups[num_groups].capacity * sizeof(Student));
            groups[num_groups].size = 0;
            group_idx = num_groups;
            num_groups++;
        }
        
        // Ensure we have enough capacity
        if (groups[group_idx].size >= groups[group_idx].capacity) {
            groups[group_idx].capacity *= 2;
            groups[group_idx].students = (Student*)realloc(groups[group_idx].students, 
                                                        groups[group_idx].capacity * sizeof(Student));
        }
        
        // Add student to group
        groups[group_idx].students[groups[group_idx].size] = students[i];
        groups[group_idx].size++;
    }
    
    // Sort groups by size (largest first)
    for (int i = 0; i < num_groups; i++) {
        for (int j = i + 1; j < num_groups; j++) {
            if (groups[j].size > groups[i].size) {
                StudentGroup temp = groups[i];
                groups[i] = groups[j];
                groups[j] = temp;
            }
        }
    }
    
    // Allocate memory for classes
    *classes = (Student**)malloc(num_classes * sizeof(Student*));
    *class_sizes = (int*)calloc(num_classes, sizeof(int));
    
    for (int i = 0; i < num_classes; i++) {
        (*classes)[i] = (Student*)malloc(num_students * sizeof(Student)); // Max possible size
    }
    
    // Distribute groups to classes
    for (int g = 0; g < num_groups; g++) {
        StudentGroup *group = &groups[g];
        
        // Find classes with minimum size
        int min_size = INT_MAX;
        int *candidate_indices = (int*)malloc(num_classes * sizeof(int));
        int num_candidates = 0;
        
        for (int i = 0; i < num_classes; i++) {
            if ((*class_sizes)[i] < min_size) {
                min_size = (*class_sizes)[i];
                num_candidates = 0;
                candidate_indices[num_candidates++] = i;
            } else if ((*class_sizes)[i] == min_size) {
                candidate_indices[num_candidates++] = i;
            }
        }
        
        // Find best class based on cost
        int best_index = candidate_indices[0];
        double best_cost = compute_group_cost((*classes)[best_index], (*class_sizes)[best_index], 
                                            group->students, group->size);
        
        for (int i = 1; i < num_candidates; i++) {
            int idx = candidate_indices[i];
            double cost = compute_group_cost((*classes)[idx], (*class_sizes)[idx], 
                                          group->students, group->size);
            
            if (cost < best_cost) {
                best_cost = cost;
                best_index = idx;
            }
        }
        
        // Add group to best class
        for (int i = 0; i < group->size; i++) {
            (*classes)[best_index][(*class_sizes)[best_index]++] = group->students[i];
        }
        
        free(candidate_indices);
    }
    
    // Free memory
    union_find_free(&uf);
    
    for (int i = 0; i < name_to_index_size; i++) {
        free(name_to_index_keys[i]);
    }
    free(name_to_index_keys);
    free(name_to_index_values);
    
    for (int i = 0; i < num_groups; i++) {
        free(groups[i].students);
    }
    free(groups);
}

static char *compute_stats(Student *class_students, int num_students) {
    int count_m = 0;
    int count_w = 0;
    
    // Count elementary schools
    char **grundschule_keys = (char**)malloc(num_students * sizeof(char*));
    int *grundschule_counts = (int*)calloc(num_students, sizeof(int));
    int grundschule_size = 0;
    
    // Count BG Gutachten
    char **bg_keys = (char**)malloc(num_students * sizeof(char*));
    int *bg_counts = (int*)calloc(num_students, sizeof(int));
    int bg_size = 0;
    
    for (int i = 0; i < num_students; i++) {
        // Gender count
        if (class_students[i].gender != NULL) {
            if (str_equal_ignore_case(class_students[i].gender, "m")) {
                count_m++;
            } else if (str_equal_ignore_case(class_students[i].gender, "w")) {
                count_w++;
            }
        }
        
        // Elementary school count
        if (class_students[i].elementary_school != NULL) {
            int gs_idx = -1;
            for (int j = 0; j < grundschule_size; j++) {
                if (str_equal_ignore_case(grundschule_keys[j], class_students[i].elementary_school)) {
                    gs_idx = j;
                    break;
                }
            }
            
            if (gs_idx == -1) {
                grundschule_keys[grundschule_size] = str_dup(class_students[i].elementary_school);
                gs_idx = grundschule_size;
                grundschule_size++;
            }
            
            grundschule_counts[gs_idx]++;
        }
        
        // BG Gutachten count
        if (class_students[i].bg_gutachten != NULL) {
            int bg_idx = -1;
            for (int j = 0; j < bg_size; j++) {
                if (str_equal_ignore_case(bg_keys[j], class_students[i].bg_gutachten)) {
                    bg_idx = j;
                    break;
                }
            }
            
            if (bg_idx == -1) {
                bg_keys[bg_size] = str_dup(class_students[i].bg_gutachten);
                bg_idx = bg_size;
                bg_size++;
            }
            
            bg_counts[bg_idx]++;
        }
    }
    
    // Build stats string
    int buffer_size = 4096; // Initial size
    char *stats = (char*)malloc(buffer_size);
    int pos = 0;
    
    pos += snprintf(stats + pos, buffer_size - pos, 
                   "Gender distribution: m = %d, w = %d\n\n", count_m, count_w);
    
    pos += snprintf(stats + pos, buffer_size - pos, "Grundschule distribution:\n");
    for (int i = 0; i < grundschule_size; i++) {
        pos += snprintf(stats + pos, buffer_size - pos, 
                       "  %s: %d\n", grundschule_keys[i], grundschule_counts[i]);
    }
    
    pos += snprintf(stats + pos, buffer_size - pos, "\nBG Gutachten distribution:\n");
    for (int i = 0; i < bg_size; i++) {
        pos += snprintf(stats + pos, buffer_size - pos, 
                       "  %s: %d\n", bg_keys[i], bg_counts[i]);
    }
    
    // Free memory
    for (int i = 0; i < grundschule_size; i++) {
        free(grundschule_keys[i]);
    }
    free(grundschule_keys);
    free(grundschule_counts);
    
    for (int i = 0; i < bg_size; i++) {
        free(bg_keys[i]);
    }
    free(bg_keys);
    free(bg_counts);
    
    return stats;
}

// ===========================
// GUI Components
// ===========================

static void show_error_dialog(GtkWindow *parent, const char *message) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", message);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_show(dialog, parent);
    g_object_unref(dialog);
}

static GtkWidget *create_student_treeview(Student *students, int num_students) {
    if (!students || num_students <= 0) {
        return NULL;
    }
    
    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    
    GtkListStore *store = gtk_list_store_new(5, 
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, 
        G_TYPE_STRING, G_TYPE_STRING);
    
    GtkTreeIter iter;
    for (int i = 0; i < num_students; i++) {
        Student *student = &students[i];
        if (!student) continue;
        
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
            0, student->first_name ? student->first_name : "",
            1, student->last_name ? student->last_name : "",
            2, student->gender ? student->gender : "",
            3, student->elementary_school ? student->elementary_school : "",
            4, student->bg_gutachten ? student->bg_gutachten : "",
            -1);
    }
    
    GtkWidget *treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    
    const char *columns[] = {"Vorname", "Nachname", "Geschlecht", "Grundschule", "BG-Gutachten"};
    for (int i = 0; i < 5; i++) {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *column = gtk_tree_view_column_new();
        gtk_tree_view_column_set_title(column, columns[i]);
        gtk_tree_view_column_pack_start(column, renderer, TRUE);
        gtk_tree_view_column_add_attribute(column, renderer, "text", i);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    }
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), treeview);
    return scrolled_window;
}

static void update_rule_textview(GtkTextView *textview, GArray *rules) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(textview);
    gtk_text_buffer_set_text(buffer, "", -1);
    
    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(buffer, &iter);
    
    for (guint i = 0; i < rules->len; i++) {
        Rule *rule = &g_array_index(rules, Rule, i);
        char *text = g_strdup_printf("%s und %s sollen in dieselbe Klasse\n", rule->student_a, rule->student_b);
        gtk_text_buffer_insert(buffer, &iter, text, -1);
        g_free(text);
    }
}

static void add_rule_dialog_response(GtkDialog *dialog, int response, gpointer user_data) {
    if (response == GTK_RESPONSE_OK) {
        GtkWidget *combo_a = g_object_get_data(G_OBJECT(dialog), "combo_a");
        GtkWidget *combo_b = g_object_get_data(G_OBJECT(dialog), "combo_b");
        GArray *rules = g_object_get_data(G_OBJECT(dialog), "rules");
        GtkWidget *rule_textview = g_object_get_data(G_OBJECT(dialog), "rule_textview");
        GtkNotebook *notebook = g_object_get_data(G_OBJECT(dialog), "notebook");
        Student *students = g_object_get_data(G_OBJECT(dialog), "students");
        int num_students = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dialog), "num_students"));
        void (*update_tabs_callback)(GtkNotebook*, Student*, int, GArray*, int) = 
            g_object_get_data(G_OBJECT(dialog), "update_tabs_callback");
        
        char *name_a = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(combo_a));
        char *name_b = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(combo_b));
        
        if (name_a && name_b && strcmp(name_a, name_b) != 0) {
            Rule rule = {str_dup(name_a), str_dup(name_b)};
            g_array_append_val(rules, rule);
            update_rule_textview(GTK_TEXT_VIEW(rule_textview), rules);
            update_tabs_callback(notebook, students, num_students, rules, 0);
        }
        
        g_free(name_a);
        g_free(name_b);
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void open_add_rule_dialog(GtkWindow *parent, Student *students, int num_students, 
                               GArray *rules, GtkWidget *rule_textview, GtkNotebook *notebook,
                               void (*update_tabs_callback)(GtkNotebook*, Student*, int, GArray*, int)) {
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Regel hinzufügen");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(dialog), header);
    
    GtkWidget *cancel_button = gtk_button_new_with_label("Abbrechen");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), cancel_button);
    g_signal_connect_swapped(cancel_button, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    
    GtkWidget *add_button = gtk_button_new_with_label("Hinzufügen");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), add_button);
    g_signal_connect(add_button, "clicked", G_CALLBACK(add_rule_dialog_response), dialog);
    
    GtkWidget *content_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_start(content_area, 5);
    gtk_widget_set_margin_end(content_area, 5);
    gtk_widget_set_margin_top(content_area, 5);
    gtk_widget_set_margin_bottom(content_area, 5);
    
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    
    GtkWidget *label_a = gtk_label_new("Schüler A:");
    GtkWidget *label_b = gtk_label_new("Schüler B:");
    GtkWidget *combo_a = gtk_drop_down_new(NULL, NULL);
    GtkWidget *combo_b = gtk_drop_down_new(NULL, NULL);
    
    gtk_grid_attach(GTK_GRID(grid), label_a, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), combo_a, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_b, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), combo_b, 1, 1, 1, 1);
    
    GListStore *store_a = g_list_store_new(G_TYPE_STRING);
    GListStore *store_b = g_list_store_new(G_TYPE_STRING);
    
    for (int i = 0; i < num_students; i++) {
        char *full_name = g_strdup_printf("%s %s", students[i].first_name, students[i].last_name);
        g_list_store_append(store_a, full_name);
        g_list_store_append(store_b, full_name);
        g_free(full_name);
    }
    
    GtkSingleSelection *selection_a = gtk_single_selection_new(G_LIST_MODEL(store_a));
    GtkSingleSelection *selection_b = gtk_single_selection_new(G_LIST_MODEL(store_b));
    
    gtk_drop_down_set_model(GTK_DROP_DOWN(combo_a), G_LIST_MODEL(store_a));
    gtk_drop_down_set_model(GTK_DROP_DOWN(combo_b), G_LIST_MODEL(store_b));
    
    g_object_unref(store_a);
    g_object_unref(store_b);
    g_object_unref(selection_a);
    g_object_unref(selection_b);
    
    gtk_box_append(GTK_BOX(content_area), grid);
    gtk_window_set_child(GTK_WINDOW(dialog), content_area);
    
    g_object_set_data(G_OBJECT(dialog), "combo_a", combo_a);
    g_object_set_data(G_OBJECT(dialog), "combo_b", combo_b);
    g_object_set_data(G_OBJECT(dialog), "rules", rules);
    g_object_set_data(G_OBJECT(dialog), "rule_textview", rule_textview);
    g_object_set_data(G_OBJECT(dialog), "notebook", notebook);
    g_object_set_data(G_OBJECT(dialog), "students", students);
    g_object_set_data(G_OBJECT(dialog), "num_students", GINT_TO_POINTER(num_students));
    g_object_set_data(G_OBJECT(dialog), "update_tabs_callback", update_tabs_callback);
    
    gtk_widget_set_visible(dialog, TRUE);
}

static void update_tabs(GtkNotebook *notebook, Student *students, int num_students, GArray *rules, int num_classes) {
    // Clear existing tabs
    while (gtk_notebook_get_n_pages(notebook) > 0) {
        gtk_notebook_remove_page(notebook, 0);
    }
    
    if (students == NULL || num_students == 0) {
        show_error_dialog(NULL, "Keine Schülerdaten verfügbar.");
        return;
    }
    
    // Distribute students into classes
    Student **classes = NULL;
    int *class_sizes = NULL;
    
    if (rules && rules->len > 0) {
        distribute_students_with_rules(students, num_students, 
            (Rule*)rules->data, rules->len, num_classes, &classes, &class_sizes);
    } else {
        distribute_students_optimized(students, num_students, num_classes, &classes, &class_sizes);
    }
    
    if (!classes || !class_sizes) {
        show_error_dialog(NULL, "Fehler bei der Klasseneinteilung.");
        return;
    }
    
    // Add tabs for each class
    for (int i = 0; i < num_classes; i++) {
        if (!classes[i] || class_sizes[i] <= 0) continue;
        
        char *label = g_strdup_printf("Klasse %d", i + 1);
        GtkWidget *scrolled_window = gtk_scrolled_window_new();
        GtkWidget *treeview = create_student_treeview(classes[i], class_sizes[i]);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), treeview);
        gtk_notebook_append_page(notebook, scrolled_window, gtk_label_new(label));
        g_free(label);
    }
    
    // Add statistics tab
    GtkWidget *stats_frame = gtk_frame_new("Klassenstatistiken");
    GtkWidget *stats_textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(stats_textview), FALSE);
    
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(stats_textview));
    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(buffer, &iter);
    
    // Add statistics for each class
    for (int i = 0; i < num_classes; i++) {
        if (!classes[i] || class_sizes[i] <= 0) continue;
        
        char *stats = compute_stats(classes[i], class_sizes[i]);
        if (stats) {
            char *header = g_strdup_printf("\nKlasse %d:\n", i + 1);
            gtk_text_buffer_insert(buffer, &iter, header, -1);
            gtk_text_buffer_insert(buffer, &iter, stats, -1);
            g_free(header);
            g_free(stats);
        }
    }
    
    gtk_frame_set_child(GTK_FRAME(stats_frame), stats_textview);
    gtk_widget_set_margin_top(stats_frame, 5);
    gtk_widget_set_margin_bottom(stats_frame, 5);
    gtk_notebook_append_page(notebook, stats_frame, gtk_label_new("Statistiken"));
    
    // Clean up
    for (int i = 0; i < num_classes; i++) {
        if (classes[i]) free(classes[i]);
    }
    free(classes);
    free(class_sizes);
    
    gtk_widget_set_visible(GTK_WIDGET(notebook), TRUE);
}

// ===========================
// Main Sorter Window
// ===========================

typedef struct {
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *rule_textview;
    GArray *rules;
    Student *students;
    int num_students;
} SorterWindow;

static void add_rule_button_clicked(GtkButton *button, gpointer user_data) {
    SorterWindow *sorter_window = user_data;
    open_add_rule_dialog(
        GTK_WINDOW(sorter_window->window),
        sorter_window->students,
        sorter_window->num_students,
        sorter_window->rules,
        sorter_window->rule_textview,
        GTK_NOTEBOOK(sorter_window->notebook),
        update_tabs
    );
}

static GtkWidget *create_sorter_window(GtkApplication *app, Student *students, int num_students, int num_classes) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Klasseneinteilung");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_start(vbox, 5);
    gtk_widget_set_margin_end(vbox, 5);
    gtk_widget_set_margin_top(vbox, 5);
    gtk_widget_set_margin_bottom(vbox, 5);
    
    // Create rules array
    GArray *rules = g_array_new(FALSE, FALSE, sizeof(Rule));
    
    // Create rule textview
    GtkWidget *rule_textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(rule_textview), FALSE);
    
    // Create add rule button
    GtkWidget *add_rule_button = gtk_button_new_with_label("Regel hinzufügen");
    g_signal_connect(add_rule_button, "clicked", G_CALLBACK(add_rule_button_clicked), NULL);
    gtk_box_append(GTK_BOX(vbox), add_rule_button);
    
    // Create notebook for class tabs
    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_append(GTK_BOX(vbox), notebook);
    
    gtk_window_set_child(GTK_WINDOW(window), vbox);
    
    // Set data for callbacks
    SorterWindow *sorter_window = g_new(SorterWindow, 1);
    sorter_window->window = window;
    sorter_window->notebook = notebook;
    sorter_window->rule_textview = rule_textview;
    sorter_window->rules = rules;
    sorter_window->students = students;
    sorter_window->num_students = num_students;
    g_object_set_data(G_OBJECT(add_rule_button), "sorter_window", sorter_window);
    
    // Update tabs with initial distribution
    update_tabs(GTK_NOTEBOOK(notebook), students, num_students, rules, num_classes);
    
    return window;
}

// ===========================
// Browse Button Handler
// ===========================

static void file_chooser_response(GtkDialog *dialog, int response, gpointer user_data) {
    g_print("File chooser response: %d\n", response);
    
    if (response == GTK_RESPONSE_ACCEPT) {
        g_print("User accepted file selection\n");
        
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        if (!chooser) {
            g_print("Error: chooser is NULL\n");
            return;
        }
        
        GFile *file = gtk_file_chooser_get_file(chooser);
        if (!file) {
            g_print("Error: file is NULL\n");
            return;
        }
        
        char *path = g_file_get_path(file);
        if (!path) {
            g_print("Error: path is NULL\n");
            g_object_unref(file);
            return;
        }
        
        g_print("Selected file path: %s\n", path);
        
        // Convert Windows backslashes to forward slashes for consistency
        for (char *p = path; *p; p++) {
            if (*p == '\\') *p = '/';
        }
        
        GtkWidget *entry = user_data;
        if (!entry) {
            g_print("Error: entry widget is NULL\n");
            g_free(path);
            g_object_unref(file);
            return;
        }
        
        g_print("Setting entry text to: %s\n", path);
        gtk_editable_set_text(GTK_EDITABLE(entry), path);
        
        g_free(path);
        g_object_unref(file);
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void browse_button_clicked(GtkButton *button, gpointer user_data) {
    g_print("Browse button clicked\n");
    
    GtkWidget *entry = user_data;
    if (!entry) {
        g_print("Error: entry widget is NULL\n");
        return;
    }
    g_print("Entry widget is valid\n");
    
    GtkWidget *window = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW);
    if (!window) {
        g_print("Error: could not get parent window\n");
        return;
    }
    g_print("Parent window is valid\n");
    
    g_print("Creating file chooser dialog...\n");
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Datei auswählen",
        GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "Abbrechen", GTK_RESPONSE_CANCEL,
        "Öffnen", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    // disable the native dialog on GTK3 only; on GTK4 this macro does nothing
    MAYBE_DISABLE_NATIVE_FILECHOOSER(dialog, FALSE);
    
    if (!dialog) {
        g_print("Error: could not create file chooser dialog\n");
        return;
    }
    g_print("File chooser dialog created successfully\n");
    
    g_print("Setting up file filters...\n");
    GtkFileFilter *filter = gtk_file_filter_new();
    if (filter) {
        g_print("Creating CSV filter...\n");
        gtk_file_filter_set_name(filter, "CSV Dateien");
        gtk_file_filter_add_pattern(filter, "*.csv");
        gtk_file_filter_add_pattern(filter, "*.CSV");
        g_print("Setting CSV filter...\n");
        gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), filter);
        g_print("CSV filter set successfully\n");
    } else {
        g_print("Error: could not create CSV filter\n");
    }
    
    g_print("Creating all files filter...\n");
    filter = gtk_file_filter_new();
    if (filter) {
        gtk_file_filter_set_name(filter, "Alle Dateien");
        gtk_file_filter_add_pattern(filter, "*");
        g_print("Adding all files filter...\n");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
        g_print("All files filter set successfully\n");
    } else {
        g_print("Error: could not create all files filter\n");
    }
    
    g_print("Setting dialog as modal...\n");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    g_print("Dialog set as modal\n");
    
    g_print("Setting dialog as transient...\n");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window));
    g_print("Dialog set as transient\n");
    
    g_print("Connecting response signal...\n");
    g_signal_connect(dialog, "response", G_CALLBACK(file_chooser_response), entry);
    g_print("Response signal connected\n");
    
    g_print("Showing dialog...\n");
    gtk_widget_set_visible(dialog, TRUE);
    g_print("Dialog shown\n");
}

// ===========================
// Start Button Handler
// ===========================

static void start_button_clicked(GtkButton *button, gpointer user_data) {
    GtkApplication *app = GTK_APPLICATION(g_object_get_data(G_OBJECT(button), "app"));
    GtkWidget *file_path_entry = g_object_get_data(G_OBJECT(button), "file_path_entry");
    GtkWidget *num_classes_entry = g_object_get_data(G_OBJECT(button), "num_classes_entry");
    
    const char *file_path = gtk_editable_get_text(GTK_EDITABLE(file_path_entry));
    if (strlen(file_path) == 0) {
        show_error_dialog(NULL, "Bitte wählen Sie eine Datei aus.");
        return;
    }
    
    const char *num_classes_text = gtk_editable_get_text(GTK_EDITABLE(num_classes_entry));
    int num_classes = atoi(num_classes_text);
    if (num_classes <= 0) {
        show_error_dialog(NULL, "Bitte geben Sie eine gültige Anzahl von Klassen ein.");
        return;
    }
    
    Student *students = NULL;
    int num_students = 0;
    load_students(file_path, &students, &num_students);
    
    if (students && num_students > 0) {
        GtkWidget *sorter_window = create_sorter_window(app, students, num_students, num_classes);
        gtk_widget_set_visible(sorter_window, TRUE);
    } else {
        show_error_dialog(NULL, "Fehler beim Laden der Schülerdaten.");
    }
}

// ===========================
// Start Screen
// ===========================

static void create_start_screen(GtkApplication *app) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Klasseneinteilung - Start");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 200);
    
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_widget_set_margin_start(grid, 5);
    gtk_widget_set_margin_end(grid, 5);
    gtk_widget_set_margin_top(grid, 5);
    gtk_widget_set_margin_bottom(grid, 5);
    
    GtkWidget *file_label = gtk_label_new("Schülerdatei:");
    GtkWidget *file_path_entry = gtk_entry_new();
    GtkWidget *browse_button = gtk_button_new_with_label("Durchsuchen...");
    GtkWidget *num_classes_label = gtk_label_new("Anzahl Klassen:");
    GtkWidget *num_classes_entry = gtk_entry_new();
    GtkWidget *start_button = gtk_button_new_with_label("Start");
    
    gtk_grid_attach(GTK_GRID(grid), file_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), file_path_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), browse_button, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), num_classes_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), num_classes_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), start_button, 1, 2, 1, 1);
    
    gtk_window_set_child(GTK_WINDOW(window), grid);
    
    // Set up the browse button
    g_object_set_data(G_OBJECT(browse_button), "entry", file_path_entry);
    g_signal_connect(browse_button, "clicked", G_CALLBACK(browse_button_clicked), file_path_entry);
    
    // Set up the start button
    g_object_set_data(G_OBJECT(start_button), "app", app);
    g_object_set_data(G_OBJECT(start_button), "file_path_entry", file_path_entry);
    g_object_set_data(G_OBJECT(start_button), "num_classes_entry", num_classes_entry);
    g_signal_connect(start_button, "clicked", G_CALLBACK(start_button_clicked), NULL);
    
    gtk_widget_set_visible(window, TRUE);
}

static void app_activate(GtkApplication *app, gpointer user_data) {
    // Initialize rules
    g_rules = g_array_new(FALSE, FALSE, sizeof(Rule));
    
    // Create start screen
    create_start_screen(app);
}

static void app_shutdown(GtkApplication *app, gpointer user_data) {
    // Free memory before exiting
    if (g_students != NULL) {
        free_students(g_students, g_num_students);
        g_students = NULL;
        g_num_students = 0;
    }
    
    if (g_rules != NULL) {
        for (int i = 0; i < g_rules->len; i++) {
            Rule *r = &g_array_index(g_rules, Rule, i);
            free(r->student_a);
            free(r->student_b);
        }
        g_array_free(g_rules, TRUE);
        g_rules = NULL;
    }
}

int main(int argc, char *argv[]) {
    GtkApplication *app = gtk_application_new("com.example.schoolsort", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(app_activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(app_shutdown), NULL);
    
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    
    return status;
}