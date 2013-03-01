/* cwatch.c
 * Monitor file system activity using the inotify linux kernel library
 *
 * Copyright (C) 2012, Giuseppe Leone <joebew42@gmail.com>,
 *                     Vincenzo Di Cicco <enzodicicco@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "cwatch.h"

/* Define program name and version */
char *program_name = "cwatch";
char *program_version = "0.0 00/00/0000"; // maj.rev mm/dd/yyyy

/* Events mask */
uint32_t mask = IN_ISDIR | IN_CREATE | IN_DELETE;

void print_version()
{
    printf("%s - Version: %s\n", program_name, program_version);
}

void help()
{
    printf("Usage: %s -c COMMAND [OPTIONS] -d DIRECTORY\n", program_name);
    printf("Monitors changes in a directory through inotify system call and\n");
    printf("executes the COMMAND specified with the -c option\n\n");
    printf("   -c\t the command to execute when changes occurs\n");
    printf("   -d\t the directory to monitor\n");
    printf("   -f\t indicate which type of events to monitors (e.g. -f IN_CREATE,IN_DELETE)\n");
    printf("   -l\t Log all messages through syslog\n");
    printf("   -v\t Be verbose\n");
    printf("   -h\t Output this help and exit\n");
    printf("   -V\t Output version and exit\n");
    printf("\nReports bugs to https://github.com/joebew42/cwatch\n");
}

void log_message(char *message)
{
    if (be_verbose)
        printf("%s\n", message);
    
    if (be_syslog) {
        openlog(program_name, LOG_PID, LOG_LOCAL1);
        syslog(LOG_INFO, message);
        closelog();
    }
    
    free(message);
}

void print_list(LIST *list_wd)
{
    LIST_NODE *node = list_wd->first;
    while (node) {
        WD_DATA *wd_data = (WD_DATA *) node->data;
        printf("%s, WD:%d, LNK:%d\n", wd_data->path, wd_data->wd, wd_data->symbolic_link);
        
        /* print the content of links list */
        if (wd_data->symbolic_link == 1) {
            LIST_NODE *n_node = wd_data->links->first;
            printf ("\tList of links that point to this path:\n");
            
            while (n_node) {
                char *p = (char*) n_node->data;
                printf("\t\t%s\n", p);
                n_node = n_node->next;
            }
        }
        node = node->next;
    }
}

char *resolve_real_path(const char *path)
{
    char *resolved = malloc(MAXPATHLEN + 1);
    
    realpath(path, resolved);
    
    if (resolved == NULL)
        return NULL;
    
    strcat(resolved, "/");
     
    return resolved;
}

LIST_NODE *get_from_path(char *path)
{
    LIST_NODE *node = list_wd->first;
    while (node) {
        WD_DATA *wd_data = (WD_DATA *) node->data;
        if (strcmp(path, wd_data->path) == 0)
            return node;
        node = node->next;
    }
    
    return NULL;
}

LIST_NODE *get_from_wd(int wd)
{
    LIST_NODE *node = list_wd->first;
    while (node) {
        WD_DATA *wd_data = (WD_DATA *) node->data;
        if (wd == wd_data->wd)
            return node;
        node = node->next;    
    }
    
    return NULL;
}

int parse_command_line(int argc, char *argv[])
{
    if (argc == 1) {
        help();
        return -1;
    }
    
    /* Handle command line arguments */
    int c;
    while ((c = getopt(argc, argv, "lvVnhf:c:d:")) != -1) {
        switch (c) {
        case 'c': /* Command */                
            /* Check for a valid command */
            if (strcmp(optarg, "") == 0) {
                help();
                return -1;
            }
                
            /* Store the command */
            command = malloc(strlen(optarg) + 1);
            strcpy(command, optarg);
                
            scommand = str_split(optarg, NULL);
            if (scommand == NULL) {
                printf("Unable to process the specified command!\n");
                return -1;
            }
            
            break;
                
        case 'l': /* Enable syslog */
            be_syslog = 1;
            break;
                
        case 'v': /* Be verbose */
            be_verbose = 1;
            break;
                
        case 'f': /* lib_inotify flag handling */
            /*
             * TODO optarg will be of the form "CREATE,DELETE,MODIFY"
             * use str_split with "," as a separator symbol.
             */
            break;
                
        case 'V': /* Print version and exit */
            print_version();
            return -1;
                
        case 'n': /* Easter eggs */
            be_easter = 1;
            break;
                
        case 'd': /* Directory to watch */
            if (strcmp(optarg, "") == 0) {
                help();
                return -1;
            }
            
            /* Check if the path has the ending slash */
            if (optarg[strlen(optarg)-1] != '/') {
                path = (char *) malloc(strlen(optarg) + 2);
                strcpy(path, optarg);
                strcat(path, "/");
            } else {
                path = (char *) malloc(strlen(optarg));
                strcpy(path, optarg);
            }
            
            /* Check if it is a valid directory */
            DIR *dir = opendir(path);
            if (dir == NULL) {
                help();
                return -1;
            }
            closedir(dir);
            
            /* Check if the path is absolute or not */
            if( path[0] != '/' ) {
                char *real_path = resolve_real_path(path);
                free(path);
                path = real_path;
            }
            
            break;
                
        case 'h': /* Print the help */
                
        default:
            help();
            return -1;
        }
    }
    
    if (path == NULL || command == NULL) {
        help();
        return -1;
    }
    
    return 0;
}

int watch(char *real_path, char *symlink)
{
    /* Add initial path to the watch list */
    LIST_NODE *node = add_to_watch_list(real_path, symlink);
    if (node == NULL)
        return -1;
    
    /* Temporary list to perform a BFS directory traversing */
    LIST *list = list_init();
    list_push(list, (void *) real_path);
    
    DIR *dir_stream;
    struct dirent *dir;
    
    while (list->first != NULL) {
        /* Directory to watch */
        char *p = (char*) list_pop(list);
        
        dir_stream = opendir(p);
        
        if (dir_stream == NULL) {
            printf("UNABLE TO OPEN DIRECTORY:\t\"%s\" -> %d\n", p, errno);
            return -1;
        }
        
        /* Traverse directory */
        while (dir = readdir(dir_stream)) {
            if (dir->d_type == DT_DIR
                && strcmp(dir->d_name, ".") != 0
                && strcmp(dir->d_name, "..") != 0)
            {
                /* Absolute path to watch */
                char *path_to_watch = (char *) malloc(strlen(p) + strlen(dir->d_name) + 2);
                strcpy(path_to_watch, p);
                strcat(path_to_watch, dir->d_name);
                strcat(path_to_watch, "/");
                
                /* Append to watched resources */
                add_to_watch_list(path_to_watch, NULL);
				                
                /* Continue directory traversing */
                list_push(list, (void*) path_to_watch);
            } else if (dir->d_type == DT_LNK) {
                /* Resolve symbolic link */
                char *symlink = (char *) malloc(strlen(p) + strlen(dir->d_name) + 2);
                strcpy(symlink, p);
                strcat(symlink, dir->d_name);
                strcat(symlink, "/");
                
                char *real_path = resolve_real_path(symlink);
                
                if (real_path != NULL && opendir(real_path) != NULL) {
                    /* Append to watched resources */
                    add_to_watch_list(real_path, symlink);
                    
                    /* Continue directory traversing */
                    list_push(list, (void*) real_path);
                }
            }
        }
        closedir(dir_stream);
    }
    
    list_free(list);
    
    return 0;
}

LIST_NODE *add_to_watch_list(char *real_path, char *symlink)
{
    /* Check if the resource is already in the watch_list */
    LIST_NODE *node = get_from_path(real_path);
    
    /* If the resource is not watched yet, then add it into the watch_list */
    if (node == NULL) {
        /* Append directory to watch_list */
        int wd = inotify_add_watch(fd, real_path, mask);
        
        /* INFO Check limit in: /proc/sys/fs/inotify/max_user_watches */
        if (wd == -1) {
            printf("AN ERROR OCCURRED WHILE ADDING PATH %s:\n", real_path);
            printf("Please consider these possibilities:\n");
            printf(" - Max number of watched resources reached! See /proc/sys/fs/inotify/max_user_watches\n");
            printf(" - Resource is no more available!?\n");
            return NULL;
        }
        
        /* Create the entry */
        WD_DATA *wd_data = malloc(sizeof(WD_DATA));
        wd_data->wd = wd;
        wd_data->path = real_path;
        wd_data->links = list_init();
        wd_data->symbolic_link = (strncmp(path, real_path, strlen(path)) == 0) ? FALSE : TRUE;
        
        node = list_push(list_wd, (void*) wd_data);
        
        /* Log Message */
        char *message = malloc(MAXPATHLEN);
        sprintf(message, "WATCHING: (fd:%d,wd:%d)\t\t\"%s\"", fd, wd_data->wd, real_path);
        log_message(message);
    }

    /* Check to add symlink (if any) to the symlink list of the watched resource */
    if (node != NULL && symlink != NULL) {
        WD_DATA *wd_data = (WD_DATA*) node->data;
        
        /*
         * XXX consider that is not possible that exists two symlink with the same path!!!
         *     The code below that control for duplicate can be deleted (think about it)
         */
        bool_t found = FALSE;
        LIST_NODE *node_link = wd_data->links->first;
        while (node_link) {
            char *link = (char *) node_link->data;
            if (strcmp(link, symlink) == 0) {
                found = TRUE;
                break;
            }
            
            node_link = node_link->next;
        }
        
        if (found == FALSE) {
            list_push(wd_data->links, (void *) symlink);
            
            /* Log Message */
            char *message = malloc(MAXPATHLEN);
            sprintf(message, "ADDED SYMBOLIC LINK:\t\t\"%s\" -> \"%s\"", symlink, real_path);
            log_message(message);
        }
    }
    
    return node;
}

int exists(char* child_path, LIST *parents)
{
    if (parents == NULL || parents->first == NULL)
        return 0;
    
    LIST_NODE *node = parents->first;
    while(node) {
        char* parent_path = (char*) node->data;
        // printf("Checking for: %s \t Possible parent: %s\n", child_path, parent_path);
        if (strlen(parent_path) <= strlen(child_path)
            && strncmp(parent_path, child_path, strlen(parent_path)) == 0)
        {
            return 1; /* match! */
        }
        node = node->next;
    }
    return 0;
}

void unwatch(char *path, bool_t is_link)
{
    /* Remove the resource from watched resources */
    if (is_link == FALSE) {
        /* Retrieve the watch descriptor from path */
        LIST_NODE *node = get_from_path(path);
        if (node != NULL) {   
            WD_DATA *wd_data = (WD_DATA *) node->data;
            
            /* Log Message */
            char *message = malloc(MAXPATHLEN);
            sprintf(message, "UNWATCHING: (fd:%d,wd:%d)\t\t%s", fd, wd_data->wd, path);
            log_message(message);
            
            inotify_rm_watch(fd, wd_data->wd);
            list_remove(list_wd, node);
        }
    } else {
        /* Remove a symbolic link from watched resources */
        LIST_NODE *node = list_wd->first;
        while (node) {
            WD_DATA *wd_data = (WD_DATA*) node->data;
            
            LIST_NODE *link_node = wd_data->links->first;
            while (link_node) {
                char *p = (char*) link_node->data;
                
                /* Symbolic link match. Remove it! */
                if (strcmp(path, p) == 0) {
                    /* Log Message */
                    char *message = malloc(sizeof(MAXPATHLEN));
                    sprintf(message, "UNWATCHING SYMLINK: \t\t%s -> %s", path, wd_data->path);
                    log_message(message);
                    
                    list_remove(wd_data->links, link_node);
                    
                    /*
                     * if there is no other symbolic links that point to the
                     * watched resource then unwatch it
                     */
                    if (wd_data->links->first == NULL && wd_data->symbolic_link == 1) {
                        WD_DATA *sub_wd_data;
                        
                        /*
                         * Build temporary look-up list of resources
                         * that are pointed by some symbolic links.
                         */
                        LIST *tmp_linked_path = list_init();
                        LIST_NODE *sub_node = list_wd->first;
                        while (sub_node) {
                            sub_wd_data = (WD_DATA*) sub_node->data;
                            
                            /*
                             * If it is a PARENT or CHILD and it is referenced by some symbolic link
                             * and it is not listed into tmp_linked_path, then
                             * add it into the list and move to the next resource.
                             */
                            if ((strncmp(wd_data->path, sub_wd_data->path, strlen(wd_data->path)) == 0
                                || strncmp(wd_data->path, sub_wd_data->path, strlen(sub_wd_data->path)) == 0)
                                && sub_wd_data->links->first != NULL
                                && exists(sub_wd_data->path, tmp_linked_path) == 0)
                            {
                                /* Save current path into linked_path */
                                list_push(tmp_linked_path, (void*) sub_wd_data->path);
                            }
                            
                            /* Move to next resource */
                            sub_node = sub_node->next;
                        }
                        
                        /*
                         * Descend to all subdirectories of wd_data->path and unwatch them all
                         * only if they or it's parents are not pointed by some symbolic link, anymore
                         * (check if parent is not pointed by symlinks too).
                         */
                        sub_node = list_wd->first;
                        while (sub_node) {
                            sub_wd_data = (WD_DATA*) sub_node->data;
                            
                            /*
                             * If it is a CHILD and is NOT referenced by some symbolic link
                             * and it is not listed into tmp_linked_path, then
                             * remove the watch descriptor from the resource.
                             */
                            if (strncmp(wd_data->path, sub_wd_data->path, strlen(wd_data->path)) == 0
                                && sub_wd_data->links->first == NULL
                                && exists(sub_wd_data->path, tmp_linked_path) == 0)
                            {
                                /* Log Message */
                                char *message = malloc(MAXPATHLEN);
                                sprintf(message, "UNWATCHING: (fd:%d,wd:%d)\t\t%s", fd, sub_wd_data->wd, sub_wd_data->path);
                                log_message(message);
                                
                                inotify_rm_watch(fd, sub_wd_data->wd);
                                list_remove(list_wd, sub_node);
                            }
                            
                            /* Move to next resource */
                            sub_node = sub_node->next;
                        }
                        
                        /* Free temporay lookup list */
                        list_free(tmp_linked_path);
                    }
                    return;
                }
                link_node = link_node->next;
            }
            node = node->next;
        }
    }
}

int monitor()
{
    /* Buffer for File Descriptor */
    char buffer[EVENT_BUF_LEN];

    /* inotify_event of the event */
    struct inotify_event *event = NULL;

    /* The real path of touched directory or file */
    char *path = NULL;
    size_t len;
    int i;
    
    /* Temporary node information */
    LIST_NODE *node = NULL;
    
    /* Wait for events */
    while (len = read(fd, buffer, EVENT_BUF_LEN)) {
        if (len < 0) {
            printf("ERROR: UNABLE TO READ INOTIFY QUEUE EVENTS!!!\n");
            return -1;
        }
        
        /* index of the event into file descriptor */
        i = 0;
        while (i < len) {
            /* inotify_event */
            event = (struct inotify_event*) &buffer[i];
            
            /* Build the full path of the directory or symbolic link */
            node = get_from_wd(event->wd);
            if (node != NULL) {
                WD_DATA *wd_data = (WD_DATA *) node->data;
                
                path = malloc(strlen(wd_data->path) + strlen(event->name) + 2);
                strcpy(path, wd_data->path);
                strcat(path, event->name);
                strcat(path, "/");
            } else {
                /* Next event */
                i += EVENT_SIZE + event->len;
                continue;
            }
            
            /* IN_CREATE Event */
            if (event->mask & IN_CREATE) {
                /* execute the command */
                if (execute_command("IN_CREATE", path) == -1)
                    return -1;
                
                /* Check if it is a folder. If yes watch it */
                if (event->mask & IN_ISDIR) {
                    watch(path, NULL);
                } else {
                    /* check if it is a link. if yes watch it. */
                    bool_t is_dir = FALSE;
                    DIR *dir_stream = opendir(path);
                    if (dir_stream != NULL)
                        is_dir = TRUE;
                    closedir(dir_stream);
                    
                    if (is_dir == TRUE) {
                        /* resolve symbolic link */
                        char *real_path = resolve_real_path(path);
                        
                        /* check if the real path is already monitored */
                        LIST_NODE *node = get_from_path(real_path);
                        if (node == NULL) {
                            watch(real_path, path);
                        } else {
                            /* 
                             * Append the new symbolic link
                             * to the watched resource
                             */
                            WD_DATA *wd_data = (WD_DATA *) node->data;
                            list_push(wd_data->links, (void*) path);
                            
                            /* Log Message */
                            char *message = malloc(MAXPATHLEN);
                            sprintf(message, "ADDED SYMBOLIC LINK:\t\t\"%s\" -> \"%s\"", path, real_path);
                            log_message(message);
                        }
                    }
                }
            } else if (event->mask & IN_DELETE) {
                /* IN_DELETE event */
                
                /* execute the command */
                if (execute_command("IN_DELETE", path) == -1)
                    return -1;
                
                /* Check if it is a folder. If yes unwatch it */
                if (event->mask & IN_ISDIR) {
                    unwatch(path, FALSE);
                } else {
                    /*
                     * XXX Since it is not possible to know if the
                     *     inotify event belongs to a file or a symbolic link,
                     *     the unwatch function will be called for each file.
                     *     This is a big computational issue to be treated.
                     */
                    unwatch(path, TRUE);
                }
            }
            
            /* Next event */
            i += EVENT_SIZE + event->len;
        }
    }

    return 0;
}

STR_SPLIT_S *str_split(char *str, char *sep)
{
    if (str == NULL || strlen(str) == 0)
        return NULL;
    
    if (sep == NULL || strlen(sep) > 1) {
        sep = (char *) malloc(1);
        strcpy(sep, " ");
    }
    
    STR_SPLIT_S *r = (STR_SPLIT_S *) malloc(sizeof(STR_SPLIT_S));
    r->size = 2;

    /* count number of occurrences */
    int i = 0;
    while (str[i])
        if (str[i++] == *sep)
            ++r->size;

    r->substring = (char **) malloc(sizeof(char *) * r->size);
    
    /* find all occurrences */
    i = 0;
    r->substring[i] = strtok(str, sep);
    while (r->substring[i] != NULL)
        r->substring[++i] = strtok(NULL, sep);
    
    return r;
}

int execute_command(char *event, char *path)
{
    /* TODO maybe will be necessary to add a burst limit */
    
    /* For log purpose */
    char *message = malloc(MAXPATHLEN);

    /* Replace special pattern */
    char **command_to_execute = (char **) malloc(sizeof(char *) * scommand->size);
                                                 
    int i;
    for (i = 0; i < scommand->size - 1; ++i) {
        if (strcmp(scommand->substring[i], COMMAND_PATTERN_DIR) == 0) {
            command_to_execute[i] = path;
        } else if (strcmp(scommand->substring[i], COMMAND_PATTERN_EVENT) == 0) {
            command_to_execute[i] = event;
        } else {
            command_to_execute[i] = scommand->substring[i];
        }
    }

    /* Execute the command */
    pid_t pid = fork();
    if (pid > 0) {
        /* parent process */
        sprintf(message, "%s on %s, [%d] -> %s", event, path, pid, command);
        log_message(message); 
    } else if (pid == 0) {
        /* child process */
        if (execvp(command_to_execute[0], command_to_execute) == -1) {
            sprintf(message, "Unable to execute the specified command!");
            log_message(message);
        }
    } else {
        /* error occured */
        sprintf(message, "ERROR during the fork() !!!");
        log_message(message);
        return -1;
    }
    
    free(command_to_execute);
    
    return 0;
}
