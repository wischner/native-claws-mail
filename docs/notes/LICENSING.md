# Licensing

Blaise is licensed under the GNU General Public License, version 3 or (at your
option) any later version. `LICENSE` is the text from Claws Mail's `COPYING`.
Source headers read `GPL 3.0 or later (see: LICENSE)`.

Writer and Isaac are GPL version 2 or later, because AbiWord and Gnumeric are.
Blaise cannot be: Claws Mail is GPL version 3 or later and a derived work keeps
those terms. This is the one way Blaise differs from its siblings.

That header is also the one deliberate difference from
`docs/standards/CPP-CODING-STYLE.md`, whose sample says MIT and which is kept as
copied.

Native is MIT and OpenSSL is Apache 2.0, and both are compatible with GPL version
3. Code taken from Writer and Isaac is used here under version 3, which their "or
later" permits; it cannot flow back to them from this tree without its author's
agreement, and the author of all of it is the same person.

"Claws Mail" is the Claws Mail team's name, so the product is called Blaise. Only
the file formats keep the name, in file names such as `.claws_mark` and headers
such as `X-Claws-Account-Id`, because the formats are the point.

## Files derived from Claws Mail

Files derived from a specific Claws Mail file name it and keep its copyright
line. The GPL's notice requirement needs that, so keep those lines when moving
code. Files written from scratch carry only the project line.
