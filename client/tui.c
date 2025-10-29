#include "tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Platform-specific includes for Windows support */
#ifdef _WIN32
    #include <windows.h>
#endif

/* ============================================================================
 * Global State
 * ============================================================================ */

static bool tui_initialized = false;
static bool tui_color_enabled = true;

/* ============================================================================
 * Core Functions
 * ============================================================================ */

bool tui_init(void)
{
    if (tui_initialized)
        return true;

#ifdef _WIN32
    /* Enable ANSI escape sequences on Windows 10+ */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE)
        return false;

    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode))
        return false;

    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, dwMode))
        return false;
#endif

    tui_initialized = true;

    /* Check color support */
    const char *term = getenv("TERM");
    const char *no_color = getenv("NO_COLOR");

    if (no_color != NULL && no_color[0] != '\0') {
        tui_color_enabled = false;
    } else if (term == NULL || strcmp(term, "dumb") == 0) {
        tui_color_enabled = false;
    }

    return true;
}

bool tui_has_color_support(void)
{
    if (!tui_initialized)
        tui_init();

    return tui_color_enabled;
}

void tui_reset(void)
{
    if (tui_color_enabled)
        printf(TUI_STYLE_RESET);
    fflush(stdout);
}

/* ============================================================================
 * Formatted Output Functions
 * ============================================================================ */

void tui_print_color(const char *color, const char *format, ...)
{
    if (!tui_initialized)
        tui_init();

    if (tui_color_enabled)
        printf("%s", color);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    if (tui_color_enabled)
        printf(TUI_STYLE_RESET);

    fflush(stdout);
}

void tui_print_styled(const char *color, const char *style, const char *format, ...)
{
    if (!tui_initialized)
        tui_init();

    if (tui_color_enabled)
        printf("%s%s", style, color);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    if (tui_color_enabled)
        printf(TUI_STYLE_RESET);

    fflush(stdout);
}

void tui_print_status(tui_status_t status, const char *format, ...)
{
    if (!tui_initialized)
        tui_init();

    const char *icon = "";
    const char *color = "";

    switch (status) {
        case TUI_STATUS_SUCCESS:
            icon = "✓";
            color = TUI_COLOR_GREEN;
            break;
        case TUI_STATUS_ERROR:
            icon = "✗";
            color = TUI_COLOR_RED;
            break;
        case TUI_STATUS_WARNING:
            icon = "⚠";
            color = TUI_COLOR_YELLOW;
            break;
        case TUI_STATUS_INFO:
            icon = "ℹ";
            color = TUI_COLOR_CYAN;
            break;
        case TUI_STATUS_NEUTRAL:
            icon = "•";
            color = TUI_COLOR_WHITE;
            break;
    }

    if (tui_color_enabled)
        printf("%s%s%s ", TUI_STYLE_BOLD, color, icon);
    else
        printf("%s ", icon);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    if (tui_color_enabled)
        printf(TUI_STYLE_RESET);

    printf("\n");
    fflush(stdout);
}

/* ============================================================================
 * UI Components
 * ============================================================================ */

tui_progress_t tui_progress_create(size_t current, size_t total)
{
    tui_progress_t progress = {
        .current = current,
        .total = total,
        .width = 40,
        .fill_char = '#',  /* Use ASCII for portability */
        .empty_char = '-',  /* Use ASCII for portability */
        .show_percent = true,
        .show_numbers = true
    };
    return progress;
}

void tui_progress_bar(const tui_progress_t *progress)
{
    if (!tui_initialized)
        tui_init();

    if (progress->total == 0) {
        printf("[%*s] 0%%\n", progress->width, "");
        return;
    }

    /* Calculate percentage and filled width */
    double percent = (double)progress->current / (double)progress->total;
    if (percent > 1.0) percent = 1.0;
    if (percent < 0.0) percent = 0.0;

    int filled = (int)(percent * progress->width);
    int empty = progress->width - filled;

    /* Print opening bracket */
    printf("[");

    /* Print filled portion with color */
    if (tui_color_enabled) {
        if (percent >= 1.0)
            printf(TUI_COLOR_GREEN);
        else if (percent >= 0.5)
            printf(TUI_COLOR_CYAN);
        else
            printf(TUI_COLOR_YELLOW);
    }

    for (int i = 0; i < filled; i++)
        printf("%c", progress->fill_char);

    if (tui_color_enabled)
        printf(TUI_STYLE_RESET);

    /* Print empty portion */
    if (tui_color_enabled)
        printf(TUI_COLOR_BRIGHT_BLACK);

    for (int i = 0; i < empty; i++)
        printf("%c", progress->empty_char);

    if (tui_color_enabled)
        printf(TUI_STYLE_RESET);

    /* Print closing bracket */
    printf("]");

    /* Print percentage */
    if (progress->show_percent) {
        printf(" %5.1f%%", percent * 100.0);
    }

    /* Print current/total numbers */
    if (progress->show_numbers) {
        char current_buf[32];
        char total_buf[32];
        tui_format_bytes(progress->current, current_buf, sizeof(current_buf));
        tui_format_bytes(progress->total, total_buf, sizeof(total_buf));
        printf(" (%s / %s)", current_buf, total_buf);
    }

    fflush(stdout);
}

void tui_separator(int width, char char_)
{
    if (!tui_initialized)
        tui_init();

    if (width <= 0)
        width = tui_get_terminal_width();

    if (tui_color_enabled)
        printf(TUI_COLOR_BRIGHT_BLACK);

    for (int i = 0; i < width; i++)
        printf("%c", char_);

    if (tui_color_enabled)
        printf(TUI_STYLE_RESET);

    printf("\n");
    fflush(stdout);
}

void tui_header(const char *title, int width)
{
    if (!tui_initialized)
        tui_init();

    if (width <= 0)
        width = tui_get_terminal_width();

    int title_len = strlen(title);
    int padding = (width - title_len - 2) / 2;
    if (padding < 0) padding = 0;

    tui_separator(width, '=');

    printf("%*s", padding, "");

    if (tui_color_enabled)
        printf("%s%s", TUI_STYLE_BOLD, TUI_COLOR_CYAN);

    printf(" %s ", title);

    if (tui_color_enabled)
        printf(TUI_STYLE_RESET);

    printf("\n");

    tui_separator(width, '=');
}

void tui_menu_option(int number, const char *text, const char *description)
{
    if (!tui_initialized)
        tui_init();

    if (tui_color_enabled)
        printf("%s%s%d.%s ", TUI_STYLE_BOLD, TUI_COLOR_CYAN, number, TUI_STYLE_RESET);
    else
        printf("%d. ", number);

    if (tui_color_enabled)
        printf("%s", TUI_COLOR_WHITE);

    printf("%s", text);

    if (tui_color_enabled)
        printf(TUI_STYLE_RESET);

    if (description != NULL) {
        if (tui_color_enabled)
            printf(" %s- %s%s", TUI_COLOR_BRIGHT_BLACK, description, TUI_STYLE_RESET);
        else
            printf(" - %s", description);
    }

    printf("\n");
    fflush(stdout);
}

void tui_key_value(const char *key, const char *value, int key_width)
{
    if (!tui_initialized)
        tui_init();

    if (tui_color_enabled)
        printf("%s%-*s:%s ", TUI_COLOR_BRIGHT_BLACK, key_width, key, TUI_STYLE_RESET);
    else
        printf("%-*s: ", key_width, key);

    if (tui_color_enabled)
        printf("%s%s%s", TUI_STYLE_BOLD, value, TUI_STYLE_RESET);
    else
        printf("%s", value);

    printf("\n");
    fflush(stdout);
}

void tui_box(const char *title, const char **content, int num_lines, int width)
{
    if (!tui_initialized)
        tui_init();

    if (width <= 0)
        width = tui_get_terminal_width() - 4;

    /* Top border */
    if (tui_color_enabled)
        printf(TUI_COLOR_BRIGHT_BLACK);

    printf("┌");
    for (int i = 0; i < width - 2; i++)
        printf("─");
    printf("┐\n");

    /* Title (if provided) */
    if (title != NULL) {
        printf("│ ");
        if (tui_color_enabled)
            printf("%s%s%-*s%s", TUI_STYLE_RESET, TUI_STYLE_BOLD, width - 4, title, TUI_COLOR_BRIGHT_BLACK);
        else
            printf("%-*s", width - 4, title);
        printf(" │\n");

        /* Separator after title */
        printf("├");
        for (int i = 0; i < width - 2; i++)
            printf("─");
        printf("┤\n");
    }

    /* Content */
    for (int i = 0; i < num_lines; i++) {
        printf("│ ");
        if (tui_color_enabled)
            printf("%s%-*s%s", TUI_STYLE_RESET, width - 4, content[i], TUI_COLOR_BRIGHT_BLACK);
        else
            printf("%-*s", width - 4, content[i]);
        printf(" │\n");
    }

    /* Bottom border */
    printf("└");
    for (int i = 0; i < width - 2; i++)
        printf("─");
    printf("┘");

    if (tui_color_enabled)
        printf(TUI_STYLE_RESET);

    printf("\n");
    fflush(stdout);
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

char *tui_format_bytes(size_t bytes, char *buffer, size_t bufsize)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = (double)bytes;

    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }

    if (unit_index == 0) {
        snprintf(buffer, bufsize, "%zu %s", bytes, units[unit_index]);
    } else {
        snprintf(buffer, bufsize, "%.1f %s", size, units[unit_index]);
    }

    return buffer;
}

int tui_get_terminal_width(void)
{
#ifdef TIOCGWINSZ
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
        return ws.ws_col;
    }
#endif
    return 80; /* Default fallback */
}

void tui_clear_screen(void)
{
    if (!tui_initialized)
        tui_init();

    if (tui_color_enabled)
        printf(TUI_CLEAR_SCREEN TUI_CURSOR_HOME);
    else
        printf("\n\n\n");

    fflush(stdout);
}

void tui_clear_line(void)
{
    if (!tui_initialized)
        tui_init();

    if (tui_color_enabled)
        printf("\r" TUI_CLEAR_LINE);
    else
        printf("\r");

    fflush(stdout);
}

void tui_cursor_up(int n)
{
    if (!tui_initialized)
        tui_init();

    if (tui_color_enabled && n > 0)
        printf("\033[%dA", n);

    fflush(stdout);
}

void tui_cursor_show(void)
{
    if (!tui_initialized)
        tui_init();

    if (tui_color_enabled)
        printf(TUI_CURSOR_SHOW);

    fflush(stdout);
}

void tui_cursor_hide(void)
{
    if (!tui_initialized)
        tui_init();

    if (tui_color_enabled)
        printf(TUI_CURSOR_HIDE);

    fflush(stdout);
}
