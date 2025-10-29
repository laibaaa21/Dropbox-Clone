#ifndef TUI_H
#define TUI_H

#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * TUI Library - Terminal User Interface with ANSI Escape Codes
 * ============================================================================
 * A lightweight, dependency-free terminal formatting library providing colors,
 * styles, and interactive UI components using ANSI escape sequences.
 *
 * Design Principles:
 * - Zero dependencies (pure C)
 * - Separation of concerns (presentation layer)
 * - Cross-platform (Linux, macOS, Windows 10+)
 * - No global state
 * ============================================================================
 */

/* ============================================================================
 * ANSI Color Codes
 * ============================================================================ */

/* Foreground Colors */
#define TUI_COLOR_BLACK         "\033[30m"
#define TUI_COLOR_RED           "\033[31m"
#define TUI_COLOR_GREEN         "\033[32m"
#define TUI_COLOR_YELLOW        "\033[33m"
#define TUI_COLOR_BLUE          "\033[34m"
#define TUI_COLOR_MAGENTA       "\033[35m"
#define TUI_COLOR_CYAN          "\033[36m"
#define TUI_COLOR_WHITE         "\033[37m"

/* Bright Foreground Colors */
#define TUI_COLOR_BRIGHT_BLACK  "\033[90m"
#define TUI_COLOR_BRIGHT_RED    "\033[91m"
#define TUI_COLOR_BRIGHT_GREEN  "\033[92m"
#define TUI_COLOR_BRIGHT_YELLOW "\033[93m"
#define TUI_COLOR_BRIGHT_BLUE   "\033[94m"
#define TUI_COLOR_BRIGHT_MAGENTA "\033[95m"
#define TUI_COLOR_BRIGHT_CYAN   "\033[96m"
#define TUI_COLOR_BRIGHT_WHITE  "\033[97m"

/* Background Colors */
#define TUI_BG_BLACK            "\033[40m"
#define TUI_BG_RED              "\033[41m"
#define TUI_BG_GREEN            "\033[42m"
#define TUI_BG_YELLOW           "\033[43m"
#define TUI_BG_BLUE             "\033[44m"
#define TUI_BG_MAGENTA          "\033[45m"
#define TUI_BG_CYAN             "\033[46m"
#define TUI_BG_WHITE            "\033[47m"

/* Text Styles */
#define TUI_STYLE_RESET         "\033[0m"
#define TUI_STYLE_BOLD          "\033[1m"
#define TUI_STYLE_DIM           "\033[2m"
#define TUI_STYLE_ITALIC        "\033[3m"
#define TUI_STYLE_UNDERLINE     "\033[4m"
#define TUI_STYLE_BLINK         "\033[5m"
#define TUI_STYLE_REVERSE       "\033[7m"
#define TUI_STYLE_HIDDEN        "\033[8m"

/* Cursor Control */
#define TUI_CURSOR_UP(n)        "\033[" #n "A"
#define TUI_CURSOR_DOWN(n)      "\033[" #n "B"
#define TUI_CURSOR_FORWARD(n)   "\033[" #n "C"
#define TUI_CURSOR_BACK(n)      "\033[" #n "D"
#define TUI_CURSOR_HOME         "\033[H"
#define TUI_CURSOR_SAVE         "\033[s"
#define TUI_CURSOR_RESTORE      "\033[u"
#define TUI_CURSOR_HIDE         "\033[?25l"
#define TUI_CURSOR_SHOW         "\033[?25h"

/* Screen Control */
#define TUI_CLEAR_SCREEN        "\033[2J"
#define TUI_CLEAR_LINE          "\033[2K"
#define TUI_CLEAR_TO_EOL        "\033[K"

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/* Status types for colored output */
typedef enum {
    TUI_STATUS_SUCCESS,
    TUI_STATUS_ERROR,
    TUI_STATUS_WARNING,
    TUI_STATUS_INFO,
    TUI_STATUS_NEUTRAL
} tui_status_t;

/* Progress bar configuration */
typedef struct {
    size_t current;      /* Current progress value */
    size_t total;        /* Total/maximum value */
    int width;           /* Bar width in characters (default: 40) */
    char fill_char;      /* Character for filled portion (default: '█') */
    char empty_char;     /* Character for empty portion (default: '░') */
    bool show_percent;   /* Show percentage (default: true) */
    bool show_numbers;   /* Show current/total (default: true) */
} tui_progress_t;

/* ============================================================================
 * Core Functions
 * ============================================================================ */

/**
 * Initialize TUI library (for Windows console setup)
 * Call this once at program start. Safe to call multiple times.
 *
 * Returns: true on success, false on failure
 */
bool tui_init(void);

/**
 * Check if terminal supports ANSI colors
 *
 * Returns: true if colors are supported, false otherwise
 */
bool tui_has_color_support(void);

/**
 * Reset all terminal formatting to defaults
 */
void tui_reset(void);

/* ============================================================================
 * Formatted Output Functions
 * ============================================================================ */

/**
 * Print text with specified color
 *
 * color: ANSI color code (e.g., TUI_COLOR_GREEN)
 * format: printf-style format string
 * ...: Variable arguments
 */
void tui_print_color(const char *color, const char *format, ...);

/**
 * Print text with color and style
 *
 * color: ANSI color code
 * style: ANSI style code (e.g., TUI_STYLE_BOLD)
 * format: printf-style format string
 * ...: Variable arguments
 */
void tui_print_styled(const char *color, const char *style, const char *format, ...);

/**
 * Print a status message with appropriate color and icon
 *
 * status: Status type (SUCCESS, ERROR, WARNING, INFO)
 * format: printf-style format string
 * ...: Variable arguments
 */
void tui_print_status(tui_status_t status, const char *format, ...);

/* ============================================================================
 * UI Components
 * ============================================================================ */

/**
 * Display a progress bar
 *
 * progress: Progress bar configuration
 *
 * Example output: [████████████░░░░░░░░] 60% (120/200)
 */
void tui_progress_bar(const tui_progress_t *progress);

/**
 * Create a default progress bar configuration
 *
 * current: Current progress value
 * total: Total/maximum value
 *
 * Returns: Progress bar configuration with defaults
 */
tui_progress_t tui_progress_create(size_t current, size_t total);

/**
 * Print a horizontal separator line
 *
 * width: Width in characters (0 = auto-detect terminal width)
 * char_: Character to use for the line (e.g., '=', '-', '─')
 */
void tui_separator(int width, char char_);

/**
 * Print a bordered header
 *
 * title: Header text
 * width: Width in characters (0 = auto-detect terminal width)
 */
void tui_header(const char *title, int width);

/**
 * Print a menu option with numbering
 *
 * number: Option number
 * text: Option text
 * description: Optional description (can be NULL)
 */
void tui_menu_option(int number, const char *text, const char *description);

/**
 * Print a two-column key-value pair (aligned)
 *
 * key: Key/label text
 * value: Value text
 * key_width: Width for key column (for alignment)
 */
void tui_key_value(const char *key, const char *value, int key_width);

/**
 * Print a box/panel with border and content
 *
 * title: Box title (can be NULL)
 * content: Array of content lines
 * num_lines: Number of lines in content array
 * width: Box width (0 = auto)
 */
void tui_box(const char *title, const char **content, int num_lines, int width);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Format bytes into human-readable string (e.g., "1.5 MB")
 *
 * bytes: Number of bytes
 * buffer: Output buffer
 * bufsize: Size of output buffer
 *
 * Returns: Pointer to buffer
 */
char *tui_format_bytes(size_t bytes, char *buffer, size_t bufsize);

/**
 * Get terminal width in columns
 *
 * Returns: Terminal width, or 80 if unable to detect
 */
int tui_get_terminal_width(void);

/**
 * Clear the entire screen
 */
void tui_clear_screen(void);

/**
 * Clear the current line
 */
void tui_clear_line(void);

/**
 * Move cursor up n lines
 */
void tui_cursor_up(int n);

/**
 * Show/hide cursor
 */
void tui_cursor_show(void);
void tui_cursor_hide(void);

#endif /* TUI_H */
