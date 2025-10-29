#include "client_ui.h"
#include "tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define BANNER_WIDTH 60
#define PROMPT_PREFIX "dbc> "

/* ============================================================================
 * Startup & Connection
 * ============================================================================ */

void ui_show_banner(void)
{
    tui_init();
    printf("\n");

    tui_print_styled(TUI_COLOR_CYAN, TUI_STYLE_BOLD,
        "╔════════════════════════════════════════════════════════════╗\n");
    tui_print_styled(TUI_COLOR_CYAN, TUI_STYLE_BOLD,
        "║                                                            ║\n");
    tui_print_styled(TUI_COLOR_CYAN, TUI_STYLE_BOLD,
        "║              DROPBOX CLONE - FILE STORAGE                 ║\n");
    tui_print_styled(TUI_COLOR_CYAN, TUI_STYLE_BOLD,
        "║                                                            ║\n");
    tui_print_styled(TUI_COLOR_CYAN, TUI_STYLE_BOLD,
        "╚════════════════════════════════════════════════════════════╝\n");

    printf("\n");
}

void ui_show_connecting(const char *host, const char *port)
{
    tui_print_status(TUI_STATUS_INFO, "Connecting to %s:%s...", host, port);
}

void ui_show_connected(void)
{
    tui_print_status(TUI_STATUS_SUCCESS, "Connected successfully!");
    printf("\n");
}

void ui_show_connection_error(const char *message)
{
    tui_print_status(TUI_STATUS_ERROR, "Connection failed: %s", message);
}

/* ============================================================================
 * Authentication
 * ============================================================================ */

int ui_show_auth_menu(void)
{
    char choice[10];

    printf("\n");
    tui_header("AUTHENTICATION", BANNER_WIDTH);
    printf("\n");

    tui_menu_option(1, "SIGNUP", "Create a new account");
    tui_menu_option(2, "LOGIN", "Sign in to existing account");
    tui_menu_option(3, "QUIT", "Exit the application");

    printf("\n");
    tui_print_color(TUI_COLOR_YELLOW, "Enter your choice: ");

    if (fgets(choice, sizeof(choice), stdin) == NULL)
        return 3;

    choice[strcspn(choice, "\n")] = 0;

    if (strcmp(choice, "1") == 0)
        return 1;
    else if (strcmp(choice, "2") == 0)
        return 2;
    else if (strcmp(choice, "3") == 0)
        return 3;
    else
        return -1; /* Invalid */
}

bool ui_prompt_username(char *username, size_t bufsize)
{
    tui_print_color(TUI_COLOR_CYAN, "Username: ");

    if (fgets(username, bufsize, stdin) == NULL)
        return false;

    username[strcspn(username, "\n")] = 0;
    return true;
}

bool ui_prompt_password(char *password, size_t bufsize)
{
    tui_print_color(TUI_COLOR_CYAN, "Password: ");

    if (fgets(password, bufsize, stdin) == NULL)
        return false;

    password[strcspn(password, "\n")] = 0;
    return true;
}

void ui_show_auth_result(bool success, const char *message)
{
    printf("\n");
    if (success) {
        tui_print_status(TUI_STATUS_SUCCESS, "%s", message);
    } else {
        tui_print_status(TUI_STATUS_ERROR, "%s", message);
    }
}

/* ============================================================================
 * Main Session
 * ============================================================================ */

void ui_show_session_header(const char *username)
{
    printf("\n");
    tui_separator(BANNER_WIDTH, '=');

    if (username) {
        tui_print_styled(TUI_COLOR_GREEN, TUI_STYLE_BOLD,
                        "   Welcome, %s! Session started.\n", username);
    } else {
        tui_print_styled(TUI_COLOR_GREEN, TUI_STYLE_BOLD,
                        "   Session started.\n");
    }

    tui_separator(BANNER_WIDTH, '=');
    printf("\n");
    tui_print_color(TUI_COLOR_BRIGHT_BLACK, "Type 'help' for available commands\n");
    printf("\n");
}

void ui_show_prompt(void)
{
    tui_print_color(TUI_COLOR_BRIGHT_CYAN, PROMPT_PREFIX);
}

void ui_show_help(void)
{
    printf("\n");
    tui_header("AVAILABLE COMMANDS", BANNER_WIDTH);
    printf("\n");

    tui_print_styled(TUI_COLOR_CYAN, TUI_STYLE_BOLD, "  File Operations:\n");
    printf("    ");
    tui_print_color(TUI_COLOR_GREEN, "upload <filename>");
    printf("      - Upload a file to server\n");

    printf("    ");
    tui_print_color(TUI_COLOR_GREEN, "download <filename>");
    printf("    - Download a file from server\n");

    printf("    ");
    tui_print_color(TUI_COLOR_GREEN, "delete <filename>");
    printf("      - Delete a file from server\n");

    printf("    ");
    tui_print_color(TUI_COLOR_GREEN, "list");
    printf("                  - List all your files\n");

    printf("\n");
    tui_print_styled(TUI_COLOR_CYAN, TUI_STYLE_BOLD, "  Session:\n");
    printf("    ");
    tui_print_color(TUI_COLOR_GREEN, "help");
    printf("                  - Show this help message\n");

    printf("    ");
    tui_print_color(TUI_COLOR_GREEN, "quit");
    printf("                  - Exit the client\n");

    printf("\n");
    tui_separator(BANNER_WIDTH, '-');
    printf("\n");
}

/* ============================================================================
 * File Operations
 * ============================================================================ */

void ui_show_upload_start(const char *filename, size_t filesize)
{
    char size_str[32];
    tui_format_bytes(filesize, size_str, sizeof(size_str));

    printf("\n");
    tui_print_color(TUI_COLOR_BRIGHT_BLUE, "► ");
    printf("Uploading ");
    tui_print_styled(TUI_COLOR_WHITE, TUI_STYLE_BOLD, "'%s'", filename);
    printf(" (%s)\n", size_str);
}

void ui_show_upload_progress(size_t current, size_t total)
{
    /* Clear previous line and move cursor up */
    tui_cursor_up(1);
    tui_clear_line();

    /* Display progress bar */
    printf("  ");
    tui_progress_t progress = tui_progress_create(current, total);
    progress.width = 30;
    tui_progress_bar(&progress);
    printf("\n");
}

void ui_show_upload_result(bool success, const char *message, size_t bytes_sent)
{
    char size_str[32];
    tui_format_bytes(bytes_sent, size_str, sizeof(size_str));

    if (success) {
        tui_print_status(TUI_STATUS_SUCCESS, "Upload complete (%s)", size_str);
        if (message && strlen(message) > 0) {
            tui_print_color(TUI_COLOR_BRIGHT_BLACK, "  Server: %s", message);
        }
    } else {
        tui_print_status(TUI_STATUS_ERROR, "Upload failed: %s", message);
    }
    printf("\n");
}

void ui_show_download_start(const char *filename)
{
    printf("\n");
    tui_print_color(TUI_COLOR_BRIGHT_BLUE, "▼ ");
    printf("Downloading ");
    tui_print_styled(TUI_COLOR_WHITE, TUI_STYLE_BOLD, "'%s'", filename);
    printf("\n");
}

void ui_show_download_progress(size_t current, size_t total)
{
    if (total > 0) {
        /* Clear previous line and move cursor up */
        tui_cursor_up(1);
        tui_clear_line();

        /* Display progress bar */
        printf("  ");
        tui_progress_t progress = tui_progress_create(current, total);
        progress.width = 30;
        tui_progress_bar(&progress);
        printf("\n");
    } else {
        /* Unknown size, just show bytes */
        tui_cursor_up(1);
        tui_clear_line();

        char size_str[32];
        tui_format_bytes(current, size_str, sizeof(size_str));
        printf("  Received: %s\n", size_str);
    }
}

void ui_show_download_result(bool success, const char *message, size_t bytes_received)
{
    char size_str[32];
    tui_format_bytes(bytes_received, size_str, sizeof(size_str));

    if (success) {
        tui_print_status(TUI_STATUS_SUCCESS, "Download complete (%s)", size_str);
        if (message && strlen(message) > 0) {
            tui_print_color(TUI_COLOR_BRIGHT_BLACK, "  Server: %s", message);
        }
    } else {
        tui_print_status(TUI_STATUS_ERROR, "Download failed: %s", message);
    }
    printf("\n");
}

void ui_show_delete_result(bool success, const char *filename, const char *message)
{
    if (success) {
        tui_print_status(TUI_STATUS_SUCCESS, "Deleted '%s'", filename);
        if (message && strlen(message) > 0) {
            tui_print_color(TUI_COLOR_BRIGHT_BLACK, "  Server: %s", message);
        }
    } else {
        tui_print_status(TUI_STATUS_ERROR, "Failed to delete '%s': %s", filename, message);
    }
    printf("\n");
}

void ui_show_file_list_header(void)
{
    printf("\n");
    tui_header("YOUR FILES", BANNER_WIDTH);
    printf("\n");

    /* Table header */
    tui_print_styled(TUI_COLOR_CYAN, TUI_STYLE_BOLD, "  %-40s  %10s\n", "FILENAME", "SIZE");
    tui_separator(BANNER_WIDTH, '-');
}

void ui_show_file_entry(const char *filename, size_t filesize)
{
    char size_str[32];
    tui_format_bytes(filesize, size_str, sizeof(size_str));

    printf("  ");
    tui_print_color(TUI_COLOR_WHITE, "%-40s", filename);
    printf("  ");
    tui_print_color(TUI_COLOR_YELLOW, "%10s", size_str);
    printf("\n");
}

void ui_show_file_list_footer(int total_files, size_t total_size,
                               size_t quota_used, size_t quota_total)
{
    char size_str[32];
    char quota_used_str[32];
    char quota_total_str[32];

    tui_format_bytes(total_size, size_str, sizeof(size_str));
    tui_format_bytes(quota_used, quota_used_str, sizeof(quota_used_str));
    tui_format_bytes(quota_total, quota_total_str, sizeof(quota_total_str));

    printf("\n");
    tui_separator(BANNER_WIDTH, '-');

    /* Summary */
    printf("  ");
    tui_print_color(TUI_COLOR_BRIGHT_BLACK, "Total: ");
    tui_print_styled(TUI_COLOR_WHITE, TUI_STYLE_BOLD, "%d file%s", total_files, total_files == 1 ? "" : "s");
    printf("  ");
    tui_print_color(TUI_COLOR_BRIGHT_BLACK, "Size: ");
    tui_print_styled(TUI_COLOR_YELLOW, TUI_STYLE_BOLD, "%s", size_str);
    printf("\n");

    /* Quota bar */
    printf("  ");
    tui_print_color(TUI_COLOR_BRIGHT_BLACK, "Quota: ");

    tui_progress_t quota_progress = tui_progress_create(quota_used, quota_total);
    quota_progress.width = 25;
    quota_progress.show_numbers = false;
    tui_progress_bar(&quota_progress);

    printf(" %s / %s\n", quota_used_str, quota_total_str);

    printf("\n");
}

void ui_show_file_list_empty(void)
{
    printf("\n");
    tui_print_color(TUI_COLOR_BRIGHT_BLACK, "  (No files yet)\n");
    printf("\n");
}

/* ============================================================================
 * Errors & Warnings
 * ============================================================================ */

void ui_show_error(const char *format, ...)
{
    char buffer[512];
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    tui_print_status(TUI_STATUS_ERROR, "%s", buffer);
}

void ui_show_warning(const char *format, ...)
{
    char buffer[512];
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    tui_print_status(TUI_STATUS_WARNING, "%s", buffer);
}

void ui_show_info(const char *format, ...)
{
    char buffer[512];
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    tui_print_status(TUI_STATUS_INFO, "%s", buffer);
}

void ui_show_usage_error(const char *command __attribute__((unused)), const char *usage)
{
    tui_print_status(TUI_STATUS_ERROR, "Invalid usage");
    printf("  ");
    tui_print_color(TUI_COLOR_BRIGHT_BLACK, "Usage: ");
    tui_print_color(TUI_COLOR_GREEN, "%s", usage);
    printf("\n");
}

/* ============================================================================
 * Session End
 * ============================================================================ */

void ui_show_session_end(void)
{
    printf("\n");
    tui_separator(BANNER_WIDTH, '=');
    tui_print_styled(TUI_COLOR_YELLOW, TUI_STYLE_BOLD, "   Session ended.\n");
    tui_separator(BANNER_WIDTH, '=');
}

void ui_show_goodbye(void)
{
    printf("\n");
    tui_print_styled(TUI_COLOR_CYAN, TUI_STYLE_BOLD, "   Thank you for using Dropbox Clone!\n");
    printf("\n");
}
