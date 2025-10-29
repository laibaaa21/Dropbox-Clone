#ifndef CLIENT_UI_H
#define CLIENT_UI_H

#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Client UI Layer - Presentation Logic
 * ============================================================================
 * This layer provides high-level UI functions specific to the Dropbox client,
 * built on top of the generic TUI library.
 *
 * Separation of Concerns:
 * - client.c:     Business logic (network, file I/O, protocol)
 * - client_ui.c:  Presentation logic (what to display)
 * - tui.c:        Display primitives (how to display)
 * ============================================================================
 */

/* ============================================================================
 * Startup & Connection
 * ============================================================================ */

/**
 * Display application banner/logo
 */
void ui_show_banner(void);

/**
 * Display connecting message
 *
 * host: Server hostname
 * port: Server port
 */
void ui_show_connecting(const char *host, const char *port);

/**
 * Display connection success
 */
void ui_show_connected(void);

/**
 * Display connection error
 *
 * message: Error message
 */
void ui_show_connection_error(const char *message);

/* ============================================================================
 * Authentication
 * ============================================================================ */

/**
 * Display authentication menu
 *
 * Returns: User choice (1=SIGNUP, 2=LOGIN, 3=QUIT)
 */
int ui_show_auth_menu(void);

/**
 * Prompt for username
 *
 * username: Buffer to store username
 * bufsize: Size of username buffer
 *
 * Returns: true if input received, false on error
 */
bool ui_prompt_username(char *username, size_t bufsize);

/**
 * Prompt for password (with masking)
 *
 * password: Buffer to store password
 * bufsize: Size of password buffer
 *
 * Returns: true if input received, false on error
 */
bool ui_prompt_password(char *password, size_t bufsize);

/**
 * Display authentication result
 *
 * success: true if auth succeeded, false otherwise
 * message: Server response message
 */
void ui_show_auth_result(bool success, const char *message);

/* ============================================================================
 * Main Session
 * ============================================================================ */

/**
 * Display session header with user info
 *
 * username: Logged-in username (can be NULL)
 */
void ui_show_session_header(const char *username);

/**
 * Display main command prompt
 */
void ui_show_prompt(void);

/**
 * Display help menu with available commands
 */
void ui_show_help(void);

/* ============================================================================
 * File Operations
 * ============================================================================ */

/**
 * Display upload start message
 *
 * filename: Name of file being uploaded
 * filesize: Size of file in bytes
 */
void ui_show_upload_start(const char *filename, size_t filesize);

/**
 * Display upload progress
 *
 * current: Bytes uploaded so far
 * total: Total bytes to upload
 */
void ui_show_upload_progress(size_t current, size_t total);

/**
 * Display upload result
 *
 * success: true if upload succeeded, false otherwise
 * message: Server response message
 * bytes_sent: Number of bytes sent
 */
void ui_show_upload_result(bool success, const char *message, size_t bytes_sent);

/**
 * Display download start message
 *
 * filename: Name of file being downloaded
 */
void ui_show_download_start(const char *filename);

/**
 * Display download progress
 *
 * current: Bytes downloaded so far
 * total: Total bytes to download (0 if unknown)
 */
void ui_show_download_progress(size_t current, size_t total);

/**
 * Display download result
 *
 * success: true if download succeeded, false otherwise
 * message: Server response message
 * bytes_received: Number of bytes received
 */
void ui_show_download_result(bool success, const char *message, size_t bytes_received);

/**
 * Display delete result
 *
 * success: true if delete succeeded, false otherwise
 * filename: Name of deleted file
 * message: Server response message
 */
void ui_show_delete_result(bool success, const char *filename, const char *message);

/**
 * Display file list header
 */
void ui_show_file_list_header(void);

/**
 * Display a single file entry in the list
 *
 * filename: Name of the file
 * filesize: Size of the file in bytes
 */
void ui_show_file_entry(const char *filename, size_t filesize);

/**
 * Display file list footer
 *
 * total_files: Total number of files
 * total_size: Total size of all files
 * quota_used: Quota used in bytes
 * quota_total: Total quota in bytes
 */
void ui_show_file_list_footer(int total_files, size_t total_size,
                                size_t quota_used, size_t quota_total);

/**
 * Display empty file list message
 */
void ui_show_file_list_empty(void);

/* ============================================================================
 * Errors & Warnings
 * ============================================================================ */

/**
 * Display a generic error message
 *
 * format: printf-style format string
 * ...: Variable arguments
 */
void ui_show_error(const char *format, ...);

/**
 * Display a warning message
 *
 * format: printf-style format string
 * ...: Variable arguments
 */
void ui_show_warning(const char *format, ...);

/**
 * Display an info message
 *
 * format: printf-style format string
 * ...: Variable arguments
 */
void ui_show_info(const char *format, ...);

/**
 * Display usage/syntax error for a command
 *
 * command: Command name
 * usage: Usage string (e.g., "upload <filename>")
 */
void ui_show_usage_error(const char *command, const char *usage);

/* ============================================================================
 * Session End
 * ============================================================================ */

/**
 * Display session end message
 */
void ui_show_session_end(void);

/**
 * Display goodbye message
 */
void ui_show_goodbye(void);

#endif /* CLIENT_UI_H */
