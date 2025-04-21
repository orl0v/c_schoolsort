#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>

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
        else if (str_equal_ignore_case(headers[i], "m/w")) col_gender = i;
        else if (str_equal_ignore_case(headers[i], "Grundschule")) col_grundschule = i;
        else if (str_equal_ignore_case(headers[i], "BG Gutachten")) col_bg = i;
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
        Student *s = &class_students[i];
        
        // Gender count
        if (s->gender != NULL) {
            char *gender = str_dup(s->gender);
            str_trim(gender);
            
            if (str_equal_ignore_case(gender, "m")) {
                count_m++;
            } else if (str_equal_ignore_case(gender, "w")) {
                count_w++;
            }
            
            free(gender);
        }
        
        // Elementary school count
        char *gs = str_is_empty(s->elementary_school) ? "Unknown" : s->elementary_school;
        int gs_idx = -1;
        
        for (int j = 0; j < grundschule_size; j++) {
            if (str_equal_ignore_case(grundschule_keys[j], gs)) {
                gs_idx = j;
                break;
            }
        }
        
        if (gs_idx == -1) {
            grundschule_keys[grundschule_size] = str_dup(gs);
            gs_idx = grundschule_size;
            grundschule_size++;
        }
        
        grundschule_counts[gs_idx]++;
        
        // BG Gutachten count
        char *bg = str_is_empty(s->bg_gutachten) ? "Unknown" : s->bg_gutachten;
        int bg_idx = -1;
        
        for (int j = 0; j < bg_size; j++) {
            if (str_equal_ignore_case(bg_keys[j], bg)) {
                bg_idx = j;
                break;
            }
        }
        
        if (bg_idx == -1) {
            bg_keys[bg_size] = str_dup(bg);
            bg_idx = bg_size;
            bg_size++;
        }
        
        bg_counts[bg_idx]++;
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
    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                             GTK_DIALOG_MODAL,
                                             GTK_MESSAGE_ERROR,
                                             GTK_BUTTONS_OK,
                                             "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), "Error");
    g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
    gtk_widget_show(dialog);
}

static GtkWidget *create_student_treeview(Student *students, int num_students) {
    GtkListStore *list_store = gtk_list_store_new(5, 
                                               G_TYPE_STRING,  // Vorname
                                               G_TYPE_STRING,  // Nachname
                                               G_TYPE_STRING,  // m/w
                                               G_TYPE_STRING,  // Grundschule
                                               G_TYPE_STRING); // BG Gutachten
    
    for (int i = 0; i < num_students; i++) {
        GtkTreeIter iter;
        gtk_list_store_append(list_store, &iter);
        gtk_list_store_set(list_store, &iter,
                         0, students[i].first_name,
                         1, students[i].last_name,
                         2, students[i].gender,
                         3, students[i].elementary_school,
                         4, students[i].bg_gutachten,
                         -1);
    }
    
    GtkWidget *treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(list_store));
    g_object_unref(list_store);
    
    gtk_widget_set_vexpand(treeview, TRUE);
    
    const char *columns[] = {"Vorname", "Nachname", "m/w", "Grundschule", "BG Gutachten"};
    
    for (int i = 0; i < 5; i++) {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *column = gtk_tree_view_column_new();
        
        gtk_tree_view_column_set_title(column, columns[i]);
        gtk_tree_view_column_pack_start(column, renderer, TRUE);
        gtk_tree_view_column_add_attribute(column, renderer, "text", i);
        
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    }
    
    return treeview;
}

static void update_rule_textview(GtkTextView *textview, GArray *rules) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(textview);
    GString *text = g_string_new("");
    
    for (int i = 0; i < rules->len; i++) {
        Rule *r = &g_array_index(rules, Rule, i);
        g_string_append_printf(text, "%s should be with %s\n", r->student_a, r->student_b);
    }
    
    gtk_text_buffer_set_text(buffer, text->str, text->len);
    g_string_free(text, TRUE);
}

static void add_rule_dialog_response(GtkDialog *dialog, int response, gpointer user_data) {
    if (response == GTK_RESPONSE_OK) {
        GtkComboBoxText *combo_a = GTK_COMBO_BOX_TEXT(g_object_get_data(G_OBJECT(dialog), "combo_a"));
        GtkComboBoxText *combo_b = GTK_COMBO_BOX_TEXT(g_object_get_data(G_OBJECT(dialog), "combo_b"));
        GtkWidget *rule_textview = GTK_WIDGET(g_object_get_data(G_OBJECT(dialog), "rule_textview"));
        GtkNotebook *notebook = GTK_NOTEBOOK(g_object_get_data(G_OBJECT(dialog), "notebook"));
        GtkWindow *parent_window = GTK_WINDOW(g_object_get_data(G_OBJECT(dialog), "parent_window"));
        GArray *rules = (GArray *)g_object_get_data(G_OBJECT(dialog), "rules");
        
        char *name_a = gtk_combo_box_text_get_active_text(combo_a);
        char *name_b = gtk_combo_box_text_get_active_text(combo_b);
        
        if (name_a && name_b && strcmp(name_a, name_b) == 0) {
            show_error_dialog(parent_window, "Please select two different students.");
            g_free(name_a);
            g_free(name_b);
            return;
        }
        
        if (name_a && name_b) {
            Rule new_rule;
            new_rule.student_a = str_dup(name_a);
            new_rule.student_b = str_dup(name_b);
            
            g_array_append_val(rules, new_rule);
            
            update_rule_textview(GTK_TEXT_VIEW(rule_textview), rules);
            update_tabs(notebook, g_students, g_num_students, rules, g_num_classes);
            
            g_free(name_a);
            g_free(name_b);
        }
    }
    
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void open_add_rule_dialog(GtkWindow *parent, Student *students, int num_students, 
                               GArray *rules, GtkWidget *rule_textview, GtkNotebook *notebook,
                               void (*update_tabs_callback)(GtkNotebook*, Student*, int, GArray*, int)) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Add Rule",
        parent,
        GTK_DIALOG_MODAL,
        "Add Rule", GTK_RESPONSE_OK,
        "Cancel", GTK_RESPONSE_CANCEL,
        NULL
    );
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 200);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_widget_set_margin_top(grid, 10);
    gtk_widget_set_margin_bottom(grid, 10);
    gtk_widget_set_margin_start(grid, 10);
    gtk_widget_set_margin_end(grid, 10);
    
    GtkWidget *label_a = gtk_label_new("Student A:");
    GtkWidget *label_b = gtk_label_new("Student B:");
    
    gtk_grid_attach(GTK_GRID(grid), label_a, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_b, 0, 1, 1, 1);
    
    GtkWidget *combo_a = gtk_combo_box_text_new();
    GtkWidget *combo_b = gtk_combo_box_text_new();
    
    for (int i = 0; i < num_students; i++) {
        char full_name[1024];
        sprintf(full_name, "%s %s", students[i].first_name, students[i].last_name);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_a), full_name);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_b), full_name);
    }
    
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_a), 0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_b), 0);
    
    gtk_grid_attach(GTK_GRID(grid), combo_a, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), combo_b, 1, 1, 1, 1);
    
    gtk_box_append(GTK_BOX(content_area), grid);
    
    // Save data for access in the callback
    g_object_set_data(G_OBJECT(dialog), "combo_a", combo_a);
    g_object_set_data(G_OBJECT(dialog), "combo_b", combo_b);
    g_object_set_data(G_OBJECT(dialog), "rule_textview", rule_textview);
    g_object_set_data(G_OBJECT(dialog), "notebook", notebook);
    g_object_set_data(G_OBJECT(dialog), "parent_window", parent);
    g_object_set_data(G_OBJECT(dialog), "rules", rules);
    
    g_signal_connect(dialog, "response", G_CALLBACK(add_rule_dialog_response), NULL);
    
    gtk_widget_show(dialog);
}

static void update_tabs(GtkNotebook *notebook, Student *students, int num_students, GArray *rules, int num_classes) {
    // Remove all existing tabs
    while (gtk_notebook_get_n_pages(notebook) > 0) {
        gtk_notebook_remove_page(notebook, 0);
    }
    
    // Distribute students
    Student **classes = NULL;
    int *class_sizes = NULL;
    
    if (rules->len == 0) {
        distribute_students_optimized(students, num_students, num_classes, &classes, &class_sizes);
    } else {
        Rule *rules_array = (Rule *)rules->data;
        distribute_students_with_rules(students, num_students, rules_array, rules->len, num_classes, &classes, &class_sizes);
    }
    
    // Create tabs for each class
    for (int i = 0; i < num_classes; i++) {
        GtkWidget *treeview = create_student_treeview(classes[i], class_sizes[i]);
        GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
        
        gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window), treeview);
        gtk_widget_set_vexpand(scrolled_window, TRUE);
        
        // Create stats text view
        char *stats_text = compute_stats(classes[i], class_sizes[i]);
        GtkWidget *stats_textview = gtk_text_view_new();
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(stats_textview));
        
        gtk_text_view_set_editable(GTK_TEXT_VIEW(stats_textview), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(stats_textview), GTK_WRAP_WORD);
        gtk_text_buffer_set_text(buffer, stats_text, -1);
        
        GtkWidget *stats_frame = gtk_frame_new("Klassenstatistiken");
        gtk_container_add(GTK_CONTAINER(stats_frame), stats_textview);
        gtk_widget_set_margin_top(stats_frame, 5);
        gtk_widget_set_margin_bottom(stats_frame, 5);
        
        // Pack in a vertical box
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_box_append(GTK_BOX(vbox), scrolled_window);
        gtk_box_append(GTK_BOX(vbox), stats_frame);
        
        // Add tab with class name
        char tab_label[20];
        sprintf(tab_label, "Klasse %d", i + 1);
        gtk_notebook_append_page(notebook, vbox, gtk_label_new(tab_label));
        
        // Free memory
        free(stats_text);
    }
    
    // Free memory
    for (int i = 0; i < num_classes; i++) {
        free(classes[i]);
    }
    free(classes);
    free(class_sizes);
    
    gtk_widget_show(GTK_WIDGET(notebook));
}

// ===========================
// Main Sorter Window
// ===========================

typedef struct {
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *rule_textview;
    GArray *rules;
} SorterWindow;

static void add_rule_button_clicked(GtkButton *button, gpointer user_data) {
    SorterWindow *sorter = (SorterWindow *)user_data;
    open_add_rule_dialog(GTK_WINDOW(sorter->window), g_students, g_num_students, 
                       sorter->rules, sorter->rule_textview, GTK_NOTEBOOK(sorter->notebook), 
                       update_tabs);
}

static GtkWidget *create_sorter_window(GtkApplication *app, Student *students, int num_students, int num_classes) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Klassen Unterteilung");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 400);
    
    // Create main layout
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    
    // Top panel for rule management
    GtkWidget *top_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *add_rule_button = gtk_button_new_with_label("Add Rule");
    gtk_box_append(GTK_BOX(top_hbox), add_rule_button);
    
    GtkWidget *rule_textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(rule_textview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(rule_textview), GTK_WRAP_WORD);
    
    GtkWidget *rule_frame = gtk_frame_new("Current Rules");
    gtk_frame_set_child(GTK_FRAME(rule_frame), rule_textview);
    gtk_box_append(GTK_BOX(top_hbox), rule_frame);
    
    gtk_box_append(GTK_BOX(vbox), top_hbox);
    
    // Notebook for class display
    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_append(GTK_BOX(vbox), notebook);
    
    gtk_container_add(GTK_CONTAINER(window), vbox);
    
    // Create rules array
    GArray *rules = g_array_new(FALSE, FALSE, sizeof(Rule));
    
    // Set up callback data
    SorterWindow *sorter = g_new(SorterWindow, 1);
    sorter->window = window;
    sorter->notebook = notebook;
    sorter->rule_textview = rule_textview;
    sorter->rules = rules;
    
    // Connect signals
    g_signal_connect(add_rule_button, "clicked", G_CALLBACK(add_rule_button_clicked), sorter);
    
    // Initialize tabs
    update_tabs(GTK_NOTEBOOK(notebook), students, num_students, rules, num_classes);
    
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(g_free), sorter);
    
    return window;
}

// ===========================
// Browse Button Handler
// ===========================

static void file_chooser_response(GtkDialog *dialog, int response, gpointer user_data) {
    GtkWidget *entry = GTK_WIDGET(user_data);
    
    if (response == GTK_RESPONSE_OK) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file) {
            char *path = g_file_get_path(file);
            if (path) {
                gtk_entry_set_text(GTK_ENTRY(entry), path);
                g_free(path);
            }
            g_object_unref(file);
        }
    }
    
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void browse_button_clicked(GtkButton *button, gpointer user_data) {
    GtkWidget *parent_window = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "parent_window"));
    GtkWidget *entry = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "entry"));
    
    GtkWidget *file_chooser = gtk_file_chooser_dialog_new(
        "Select CSV File",
        GTK_WINDOW(parent_window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "Open", GTK_RESPONSE_OK,
        "Cancel", GTK_RESPONSE_CANCEL,
        NULL
    );
    
    g_signal_connect(file_chooser, "response", G_CALLBACK(
        file_chooser_response), entry);
    gtk_widget_show(file_chooser);
}


// ===========================
// Start Button Handler
// ===========================

static void start_button_clicked(GtkButton *button, gpointer user_data) {
    GtkWidget *window = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "window"));
    GtkWidget *file_path_entry = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "file_path_entry"));
    GtkWidget *num_classes_entry = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "num_classes_entry"));
    GtkApplication *app = GTK_APPLICATION(g_object_get_data(G_OBJECT(button), "app"));
    
    const char *file_path = gtk_entry_get_text(GTK_ENTRY(file_path_entry));
    
    if (strlen(file_path) == 0) {
        show_error_dialog(GTK_WINDOW(window), "Please select a CSV file.");
        return;
    }
    
    const char *num_classes_text = gtk_entry_get_text(GTK_ENTRY(num_classes_entry));
    int num_classes = atoi(num_classes_text);
    
    if (num_classes <= 0) {
        show_error_dialog(GTK_WINDOW(window), "Invalid number of classes.");
        return;
    }
    
    // Load students
    load_students(file_path, &g_students, &g_num_students);
    
    if (g_students == NULL || g_num_students == 0) {
        show_error_dialog(GTK_WINDOW(window), "Error loading CSV file.");
        return;
    }
    
    // Store num_classes globally
    g_num_classes = num_classes;
    
    // Create and show sorter window
    GtkWidget *sorter_window = create_sorter_window(app, g_students, g_num_students, num_classes);
    gtk_widget_show(sorter_window);
    
    // Close start window
    gtk_window_destroy(GTK_WINDOW(window));
}

// ===========================
// Start Screen
// ===========================

static void create_start_screen(GtkApplication *app) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "CSV Class Sorter - Start Screen");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 200);
    
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_widget_set_margin_top(grid, 10);
    gtk_widget_set_margin_bottom(grid, 10);
    gtk_widget_set_margin_start(grid, 10);
    gtk_widget_set_margin_end(grid, 10);
    
    GtkWidget *label_file = gtk_label_new("CSV File:");
    gtk_grid_attach(GTK_GRID(grid), label_file, 0, 0, 1, 1);
    
    GtkWidget *file_path_entry = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(file_path_entry), FALSE);
    gtk_widget_set_hexpand(file_path_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), file_path_entry, 1, 0, 2, 1);
    
    GtkWidget *browse_button = gtk_button_new_with_label("Browse");
    gtk_grid_attach(GTK_GRID(grid), browse_button, 3, 0, 1, 1);
    
    GtkWidget *label_classes = gtk_label_new("Number of Classes:");
    gtk_grid_attach(GTK_GRID(grid), label_classes, 0, 1, 1, 1);
    
    GtkWidget *num_classes_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(num_classes_entry), "5");
    gtk_grid_attach(GTK_GRID(grid), num_classes_entry, 1, 1, 2, 1);
    
    GtkWidget *start_button = gtk_button_new_with_label("Start");
    gtk_grid_attach(GTK_GRID(grid), start_button, 1, 2, 1, 1);
    
    gtk_window_set_child(GTK_WINDOW(window), grid);
    
    // Set data for callbacks
    g_object_set_data(G_OBJECT(browse_button), "parent_window", window);
    g_object_set_data(G_OBJECT(browse_button), "entry", file_path_entry);
    
    g_object_set_data(G_OBJECT(start_button), "window", window);
    g_object_set_data(G_OBJECT(start_button), "file_path_entry", file_path_entry);
    g_object_set_data(G_OBJECT(start_button), "num_classes_entry", num_classes_entry);
    g_object_set_data(G_OBJECT(start_button), "app", app);
    
    // Connect signals
    g_signal_connect(browse_button, "clicked", G_CALLBACK(browse_button_clicked), NULL);
    g_signal_connect(start_button, "clicked", G_CALLBACK(start_button_clicked), NULL);
    
    gtk_widget_show(window);
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