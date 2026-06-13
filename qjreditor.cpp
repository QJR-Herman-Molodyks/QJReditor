// editor.cpp — A minimal CLI text editor inspired by GNU Nano
// Compile: g++ -o editor editor.cpp -lncurses
// Run:     ./editor [filename]

#include <ncurses.h>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <cstring>

// ─── Data ───────────────────────────────────────────────────────────────────

struct Editor {
    std::vector<std::string> lines;
    int cx = 0, cy = 0;          // cursor col / row in the document
    int scroll_row = 0;           // first visible row
    std::string filename;
    bool modified = false;
    std::string status_msg;

    // Search state
    std::string last_search;
    int search_row = -1, search_col = -1;
};

// ─── Helpers ────────────────────────────────────────────────────────────────

static void ensure_line(Editor& e, int row) {
    while ((int)e.lines.size() <= row)
        e.lines.push_back("");
}

static int num_rows(const Editor& e) {
    return (int)e.lines.size();
}

static void clamp_cx(Editor& e) {
    ensure_line(e, e.cy);
    int len = (int)e.lines[e.cy].size();
    if (e.cx > len) e.cx = len;
    if (e.cx < 0)   e.cx = 0;
}

// ─── File I/O ───────────────────────────────────────────────────────────────

static bool load_file(Editor& e, const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        e.lines.push_back("");
        return false;
    }
    std::string line;
    while (std::getline(f, line))
        e.lines.push_back(line);
    if (e.lines.empty())
        e.lines.push_back("");
    e.filename = path;
    return true;
}

static bool save_file(Editor& e, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    for (int i = 0; i < num_rows(e); ++i) {
        f << e.lines[i];
        if (i + 1 < num_rows(e)) f << '\n';
    }
    e.modified = false;
    e.filename  = path;
    return true;
}

// ─── Prompt (bottom bar input) ───────────────────────────────────────────────

static std::string prompt(const std::string& msg, const std::string& prefill = "") {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    std::string buf = prefill;
    int cursor_pos  = (int)buf.size();

    while (true) {
        // Draw prompt bar
        attron(A_REVERSE);
        mvhline(rows - 1, 0, ' ', cols);
        mvprintw(rows - 1, 0, "%s", msg.c_str());
        int off = (int)msg.size();
        mvprintw(rows - 1, off, "%s", buf.c_str());
        move(rows - 1, off + cursor_pos);
        attroff(A_REVERSE);
        refresh();

        int ch = getch();
        if (ch == '\n' || ch == KEY_ENTER) break;
        if (ch == 27) { buf = ""; break; }           // ESC → cancel
        if (ch == KEY_BACKSPACE || ch == 127) {
            if (cursor_pos > 0) { buf.erase(--cursor_pos, 1); }
        } else if (ch == KEY_LEFT)  { if (cursor_pos > 0) --cursor_pos; }
        else if (ch == KEY_RIGHT)   { if (cursor_pos < (int)buf.size()) ++cursor_pos; }
        else if (ch >= 32 && ch < 256) {
            buf.insert(cursor_pos++, 1, (char)ch);
        }
    }
    return buf;
}

// ─── Search ─────────────────────────────────────────────────────────────────

static void do_search(Editor& e, bool forward = true) {
    std::string term = prompt("Search: ", e.last_search);
    if (term.empty()) return;
    e.last_search = term;

    int start_row = e.cy, start_col = e.cx + (forward ? 1 : -1);
    for (int i = 0; i < num_rows(e); ++i) {
        int row = (start_row + (forward ? i : num_rows(e) - i)) % num_rows(e);
        size_t pos = forward
            ? e.lines[row].find(term, (i == 0 ? std::max(0, start_col) : 0))
            : e.lines[row].rfind(term);
        if (pos != std::string::npos) {
            e.cy = row; e.cx = (int)pos;
            e.search_row = row; e.search_col = (int)pos;
            e.status_msg = "Found: " + term;
            return;
        }
    }
    e.status_msg = "Not found: " + term;
}

// ─── Rendering ──────────────────────────────────────────────────────────────

static void draw(Editor& e) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int edit_rows = rows - 2;   // reserve top title bar + bottom status bar

    // Scroll so cursor is always visible
    if (e.cy < e.scroll_row)                 e.scroll_row = e.cy;
    if (e.cy >= e.scroll_row + edit_rows)    e.scroll_row = e.cy - edit_rows + 1;

    erase();

    // ── Title bar ──────────────────────────────────────────────────────────
    attron(A_REVERSE);
    mvhline(0, 0, ' ', cols);
    std::string title = "  MiniEdit 1.0  |  " +
        (e.filename.empty() ? "[No Name]" : e.filename) +
        (e.modified ? " [modified]" : "");
    mvprintw(0, (cols - (int)title.size()) / 2, "%s", title.c_str());
    attroff(A_REVERSE);

    // ── Document lines ─────────────────────────────────────────────────────
    for (int screen_row = 0; screen_row < edit_rows; ++screen_row) {
        int doc_row = screen_row + e.scroll_row;
        if (doc_row >= num_rows(e)) {
            attron(COLOR_PAIR(2));
            mvprintw(screen_row + 1, 0, "~");
            attroff(COLOR_PAIR(2));
            continue;
        }
        const std::string& line = e.lines[doc_row];

        // Highlight search match on this line
        if (doc_row == e.search_row && e.search_col >= 0 &&
            !e.last_search.empty() &&
            e.search_col + (int)e.last_search.size() <= (int)line.size()) {

            // Before match
            mvprintw(screen_row + 1, 0, "%.*s",
                     e.search_col, line.c_str());
            // Match (highlighted)
            attron(COLOR_PAIR(3) | A_BOLD);
            mvprintw(screen_row + 1, e.search_col, "%.*s",
                     (int)e.last_search.size(),
                     line.c_str() + e.search_col);
            attroff(COLOR_PAIR(3) | A_BOLD);
            // After match
            int after = e.search_col + (int)e.last_search.size();
            mvprintw(screen_row + 1, after, "%s", line.c_str() + after);
        } else {
            mvprintw(screen_row + 1, 0, "%s", line.c_str());
        }
    }

    // ── Status / help bar ──────────────────────────────────────────────────
    attron(A_REVERSE);
    mvhline(rows - 1, 0, ' ', cols);
    if (!e.status_msg.empty()) {
        mvprintw(rows - 1, 0, " %s", e.status_msg.c_str());
    } else {
        mvprintw(rows - 1, 0,
            " ^S Save  ^W Search  ^X Exit  ^K Cut  ^U Paste  ^G Goto  Arrows Move");
    }
    attroff(A_REVERSE);

    // ── Cursor ─────────────────────────────────────────────────────────────
    move(e.cy - e.scroll_row + 1, e.cx);
    refresh();
}

// ─── Key handling ───────────────────────────────────────────────────────────

static std::string clipboard;

static bool handle_key(Editor& e, int ch) {
    e.status_msg.clear();
    e.search_row = -1; // clear highlight on any keypress

    switch (ch) {

    // ── Movement ──────────────────────────────────────────────────────────
    case KEY_UP:
        if (e.cy > 0) { --e.cy; clamp_cx(e); }
        break;
    case KEY_DOWN:
        if (e.cy < num_rows(e) - 1) { ++e.cy; clamp_cx(e); }
        break;
    case KEY_LEFT:
        if (e.cx > 0) { --e.cx; }
        else if (e.cy > 0) { --e.cy; e.cx = (int)e.lines[e.cy].size(); }
        break;
    case KEY_RIGHT: {
        ensure_line(e, e.cy);
        int len = (int)e.lines[e.cy].size();
        if (e.cx < len) { ++e.cx; }
        else if (e.cy < num_rows(e) - 1) { ++e.cy; e.cx = 0; }
        break;
    }
    case KEY_HOME: e.cx = 0; break;
    case KEY_END:
        ensure_line(e, e.cy);
        e.cx = (int)e.lines[e.cy].size();
        break;
    case KEY_PPAGE:   // Page Up
        e.cy = std::max(0, e.cy - (LINES - 3));
        clamp_cx(e);
        break;
    case KEY_NPAGE:   // Page Down
        e.cy = std::min(num_rows(e) - 1, e.cy + (LINES - 3));
        clamp_cx(e);
        break;

    // ── Ctrl-G : Go to line ───────────────────────────────────────────────
    case 7: {
        std::string s = prompt("Go to line: ");
        if (!s.empty()) {
            int n = std::stoi(s) - 1;
            e.cy = std::max(0, std::min(num_rows(e) - 1, n));
            clamp_cx(e);
        }
        break;
    }

    // ── Editing ───────────────────────────────────────────────────────────
    case '\n': case KEY_ENTER: {
        ensure_line(e, e.cy);
        std::string rest = e.lines[e.cy].substr(e.cx);
        e.lines[e.cy].erase(e.cx);
        e.lines.insert(e.lines.begin() + e.cy + 1, rest);
        ++e.cy; e.cx = 0;
        e.modified = true;
        break;
    }
    case KEY_BACKSPACE: case 127: {
        ensure_line(e, e.cy);
        if (e.cx > 0) {
            e.lines[e.cy].erase(--e.cx, 1);
            e.modified = true;
        } else if (e.cy > 0) {
            e.cx = (int)e.lines[e.cy - 1].size();
            e.lines[e.cy - 1] += e.lines[e.cy];
            e.lines.erase(e.lines.begin() + e.cy);
            --e.cy;
            e.modified = true;
        }
        break;
    }
    case KEY_DC: {   // Delete key
        ensure_line(e, e.cy);
        int len = (int)e.lines[e.cy].size();
        if (e.cx < len) {
            e.lines[e.cy].erase(e.cx, 1);
            e.modified = true;
        } else if (e.cy < num_rows(e) - 1) {
            e.lines[e.cy] += e.lines[e.cy + 1];
            e.lines.erase(e.lines.begin() + e.cy + 1);
            e.modified = true;
        }
        break;
    }

    // ── Ctrl-K : Cut line ─────────────────────────────────────────────────
    case 11: {
        ensure_line(e, e.cy);
        clipboard = e.lines[e.cy];
        if (num_rows(e) > 1) {
            e.lines.erase(e.lines.begin() + e.cy);
            if (e.cy >= num_rows(e)) --e.cy;
        } else {
            e.lines[e.cy].clear();
        }
        e.cx = 0;
        e.modified = true;
        e.status_msg = "Line cut";
        break;
    }

    // ── Ctrl-U : Paste line ───────────────────────────────────────────────
    case 21: {
        e.lines.insert(e.lines.begin() + e.cy, clipboard);
        e.cx = 0;
        e.modified = true;
        e.status_msg = "Pasted";
        break;
    }

    // ── Ctrl-W : Search ───────────────────────────────────────────────────
    case 23:
        do_search(e);
        break;

    // ── Ctrl-S : Save ─────────────────────────────────────────────────────
    case 19: {
        if (e.filename.empty())
            e.filename = prompt("Save as: ");
        if (!e.filename.empty()) {
            if (save_file(e, e.filename))
                e.status_msg = "Saved: " + e.filename;
            else
                e.status_msg = "Error saving file!";
        }
        break;
    }

    // ── Ctrl-X : Exit ─────────────────────────────────────────────────────
    case 24: {
        if (e.modified) {
            std::string ans = prompt("Unsaved changes. Save before exit? (y/n): ");
            if (ans == "y" || ans == "Y") {
                if (e.filename.empty())
                    e.filename = prompt("Save as: ");
                if (!e.filename.empty())
                    save_file(e, e.filename);
            }
        }
        return false;   // signal exit
    }

    // ── Regular characters ────────────────────────────────────────────────
    default:
        if (ch >= 32 && ch < 256) {
            ensure_line(e, e.cy);
            e.lines[e.cy].insert(e.cx++, 1, (char)ch);
            e.modified = true;
        }
        break;
    }
    return true;
}

// ─── main ───────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    Editor e;
    if (argc >= 2) {
        load_file(e, argv[1]);
    } else {
        e.lines.push_back("");
    }

    // ncurses init
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    set_escdelay(50);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_WHITE,  COLOR_BLUE);   // title / status bar
        init_pair(2, COLOR_CYAN,   -1);            // tilde lines
        init_pair(3, COLOR_BLACK,  COLOR_YELLOW);  // search highlight
    }

    while (true) {
        clamp_cx(e);
        draw(e);
        int ch = getch();
        if (!handle_key(e, ch)) break;
    }

    endwin();
    return 0;
}
