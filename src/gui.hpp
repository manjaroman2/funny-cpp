#include <ncurses.h>
#include <panel.h>
#include <string>
#include <map>
#include <functional>
#include <fmt/core.h>
#include <magic_enum.hpp>

namespace funny
{
    enum Cursor
    {
        CURSOR_INVISIBLE = 0,
        CURSOR_VISIBLE = 1,
        CURSOR_VERY_VISIBLE = 2
    };

    class Mode
    {
    public:
        enum Value : u_int8_t
        {
            COMMAND,
            CHAT
        };

        Mode() = default;
        constexpr Mode(Value aValue) : value(aValue) {}

        const std::string text() { return fmt::format("-- {} --", magic_enum::enum_name(value)); }

        constexpr operator Value() const { return value; }
        constexpr bool operator==(const Mode &other) const { return value == other.value; }
        constexpr bool operator!=(const Mode &other) const { return value != other.value; }

    private:
        Value value;
    };

    class Gui
    {
    public:
        Gui()
        {
            root_win = initscr();
            start_color();
            cbreak();
            noecho();
            keypad(root_win, TRUE);
            mode = Mode::COMMAND;
            set_escdelay(100);
            curs_set(CURSOR_INVISIBLE);

            use_default_colors();
            transparent_color_pair = COLOR_PAIR(init_pair(0, text_color, -1));
            command_color_pair = COLOR_PAIR(init_pair(1, text_color, COLOR_WHITE));
            wbkgd(root_win, transparent_color_pair);

            y_ratio = LINES / 3;
            x_ratio = COLS / 4;
            chat_win = subwin(root_win, y_ratio - 1, COLS, LINES - y_ratio - 1, 0);
            // box(chat_win, 0, 0);
            wborder(chat_win, 0, 0, 0, ' ', 0, 0, 0, 0);
            side_win = newwin(LINES - y_ratio, x_ratio, 0, COLS - x_ratio);
            box(side_win, 0, 0);
            side_panel = new_panel(side_win);
            main_win = subwin(root_win, LINES - y_ratio, COLS - x_ratio, 0, 0);
            box(main_win, 0, 0);
            command_win = newwin(1, COLS, LINES - 1, 0);
            command_panel = new_panel(command_win);

            wbkgd(command_win, command_color_pair);
            waddstr(command_win, mode.text().c_str());

            // hide_panel(command_panel);
            update_panels();
            wrefresh(root_win);
            // wrefresh(command_win);

            init_keybinds();
        };
        ~Gui()
        {
            endwin();
        }

        void init_keybinds()
        {
            keybinds['s'] = [this]()
            {
                if (panel_hidden(side_panel))
                {
                    show_panel(side_panel);
                    wresize(main_win, LINES - y_ratio, COLS - x_ratio);
                }
                else
                {
                    hide_panel(side_panel);
                    wresize(main_win, LINES - y_ratio, COLS);
                }
                update_panels();
                wclear(main_win);
                box(main_win, 0, 0);
                wrefresh(main_win);
            };

            keybinds['c'] = [this]()
            {
                set_mode(Mode::CHAT);
            };

            keybinds['q'] = [this]()
            { close(); };

            keybinds[':'] = [this]()
            {
                curs_set(CURSOR_VISIBLE);
                typing_command = true;
                wclear(command_win);
                waddch(command_win, ':');
                wrefresh(command_win);
            };

            // placeholder
        }

        bool key_esc()
        {
            switch (this->mode)
            {
            case Mode::COMMAND:
                if (typing_command)
                {
                    typing_command = false;
                    wclear(command_win);
                    waddstr(command_win, mode.text().c_str());
                    wrefresh(command_win);
                    break;
                }
                return 1;
            case Mode::CHAT:
                set_mode(Mode::COMMAND);
                break;
            default:
                return 1;
            }
            return 0;
        }

        bool key_enter()
        {
            switch (mode)
            {
            case Mode::COMMAND:
                if (typing_command)
                {
                    wclear(main_win);
                    waddnstr(main_win, command_buffer.data(), command_buffer.size());
                    wrefresh(main_win);
                    waddstr(command_win, mode.text().c_str());
                    wrefresh(command_win);
                    command_buffer.clear();
                    typing_command = false;
                    break;
                }
                break;
            case Mode::CHAT:
                wclear(main_win);
                box(main_win, 0, 0);
                wmove(main_win, 1, 1);
                waddstr(main_win, chat_buffer.data());
                chat_buffer.clear();
                wclear(chat_win);
                box(chat_win, 0, 0);
                wrefresh(main_win);
                wrefresh(chat_win);
                break;
            default:
                return 1;
            }
            return 0;
        }

        bool key_backspace()
        {
            switch (mode)
            {
            case Mode::COMMAND:
                if (typing_command)
                {
                    wmove(command_win, 0, command_buffer.size());
                    command_buffer.pop_back();
                    wdelch(command_win);
                    wrefresh(command_win);
                }
                break;
            case Mode::CHAT:
                wdelch(chat_win);
                wrefresh(chat_win);
                break;
            default:
                return 1;
            }
            return 0;
        }

        bool key_printable(char ch)
        {
            int x, y;
            switch (mode)
            {
            case Mode::COMMAND:
                if (!typing_command)
                {
                    if (keybinds.find(ch) != keybinds.end()) // TODO: Use perfect hashmap for performance
                        keybinds[ch]();
                    break;
                }
                command_buffer.push_back(ch);
                waddch(command_win, ch);
                wrefresh(command_win);
                break;
            case Mode::CHAT:
                chat_buffer.push_back(ch);
                x = chat_buffer.size() % (chat_win->_maxx - 1) + 1;
                y = chat_buffer.size() / (chat_win->_maxx - 1) + 1;
                wmove(chat_win, y, x);
                waddch(chat_win, ch);
                wrefresh(chat_win);
                top_panel(command_panel);
                show_panel(command_panel);
                update_panels();
                // wrefresh(command_win);
                // wrefresh(root_win);
                break;
            default:
                return 1;
            }
            return 0;
        }

        void loop()
        {
            int ch, x, y;
            while (!stopped)
            {
                ch = wgetch(root_win);
                switch (ch)
                {
                case 27: // Esc
                    if (key_esc())
                        return;
                    break;
                case 10: // Enter
                    if (key_enter())
                        return;
                    break;
                case KEY_BACKSPACE: // Backspace
                    if (key_backspace())
                        return;
                    break;
                default:
                    if (ch < 32 || ch > 126)
                    { // Not a printable character
                        waddch(main_win, fmt::format("{}", ch).c_str()[0]);
                        break;
                    }
                    if (key_printable(ch))
                        return;
                    break;
                }
            }
        }

        void set_mode(Mode new_mode)
        {
            mode = new_mode;
            wclear(command_win);
            waddstr(command_win, mode.text().c_str());
            wrefresh(command_win);
        }

        void close()
        {
            stopped = true;
        }

        bool is_stopped()
        {
            return stopped;
        }

    private:
        WINDOW *root_win, *chat_win, *side_win, *main_win, *command_win;
        PANEL *side_panel, *command_panel;
        Mode mode;
        bool typing_command = false;
        bool stopped = false;
        int y_ratio, x_ratio, text_color = COLOR_RED;
        std::vector<char> chat_buffer, command_buffer;
        chtype transparent_color_pair, command_color_pair;
        std::map<char, std::function<void()>> keybinds;
    };

}
