//
// Changes an MH folder that real Claws Mail wrote, as scripts/test/claws.sh
// asks: reads a message's flags, sets others and adds a message. Claws then
// reports what it sees, which is what proves the two share the format.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <iostream>

#include "mail/mh_folder.h"

using namespace mail;

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: write_claws_folder MAIL_DIRECTORY\n";
        return 2;
    }
    mh_folder store("Mailbox", argv[1]);
    folder_item *inbox = store.find_special(item_type::inbox);
    inbox->scan();
    if (inbox->total_count != 3 || inbox->unread_count != 3) {
        std::cerr << "unexpected folder: " << inbox->total_count << " total, "
                  << inbox->unread_count << " unread\n";
        return 1;
    }
    inbox->change_flags(1, flag_marked, flag_new | flag_unread);
    inbox->change_flags(2, flag_locked, 0);
    msg_info label;
    label.set_color_label(5);
    inbox->change_flags(3, label.perm_flags, 0);
    store.add(*inbox,
              "From: Blaise <b@x.org>\nTo: me@x.org\n"
              "Subject: Written by Blaise\nMessage-ID: <b1@x>\n"
              "Date: Sat, 07 Nov 2026 11:00:00 +0000\n\nHi.\n",
              0);
    inbox->scan();
    inbox->save();
    return 0;
}
