//
// Program entry. The desktop build enters through Native's program();
// the headless build has only batch work and needs no display.
//
// Derived from Claws Mail src/main.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <iostream>

#include "app/options.h"

#ifndef BLAISE_HEADLESS
#include "main_window.h"
#endif

namespace {

// Handle what needs no window; returns an exit status when finished.
int run_common(const app::options &settings, bool &finished) {
    finished = true;
    if (!settings.error.empty()) {
        std::cerr << "blaise: " << settings.error << '\n'
                  << app::usage_text();
        return 2;
    }
    if (settings.show_help) {
        std::cout << app::usage_text();
        return 0;
    }
    if (settings.show_version) {
        std::cout << "Blaise " << BLAISE_VERSION << '\n';
        return 0;
    }
    if (settings.batch())
        return app::run_batch(settings);
    finished = false;
    return 0;
}

} // namespace

#ifdef BLAISE_HEADLESS

int main(int argc, char **argv) {
    const app::options settings = app::parse_options(argc, argv);
    bool finished = false;
    const int status = run_common(settings, finished);
    if (finished)
        return status;
    std::cerr << app::usage_text();
    return 2;
}

#else

int program(int argc, char **argv) {
    const app::options settings = app::parse_options(argc, argv);
    bool finished = false;
    const int status = run_common(settings, finished);
    if (finished)
        return status;
    ui::main_window window(settings);
    return native::app::run(window);
}

#endif
