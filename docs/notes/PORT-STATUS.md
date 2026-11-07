# Port status

What has moved from Claws Mail `9226ea612` and what has not, module by module.
**Ported** means the behavior exists and is tested. **Partial** lists its gap.
Claws Mail is about 320,000 lines; this is a first port of its core.

`make live-test` runs IMAP, TLS and SMTP against real Dovecot and Mailpit servers.
POP3 was run by hand against GreenMail: collecting, no duplicates on a second run,
and dot unstuffing, but that is not automated. No large provider such as Gmail has
been tried. Work on a copy of your mail first.

`make claws-test` runs a real Claws Mail under Xvfb to check the file formats both
ways. Blaise reads its `.claws_cache`, `.claws_mark`, `folderlist.xml` and
`accountrc`, and rewrites the first two byte for byte; those files are fixtures in
`test_store`. Blaise then changes flags and adds a message, and Claws' own
`--status-full` must report the right counts, with lock and color label intact in
the mark file it rewrites. That is one small folder, not a long-lived profile.

## Common

| Claws | Blaise | Status |
| --- | --- | --- |
| `common/utils` | `lib/util/text`, `files`, `date` | What was used |
| `common/codeconv` | `lib/util/charset`, `lib/mime/encoded_word` | Ported through iconv. No Japanese-specific fallbacks |
| `common/quoted-printable`, `md5`, base64 | `lib/util/codec`, `md5` | Ported |
| `common/unmime` | `lib/mime/encoded_word` | Ported |
| `common/uuencode` | `lib/mime/part` | Decode only |
| `common/xml`, `xmlprops` | `lib/util/xml_reader` | Reader; writers are per format |
| `common/log` | `lib/util/log` | Ported, without log files |
| `common/hooks`, `plugin` | none | Not ported: no plugins |
| `common/socket`, `session` | `lib/net/transport`, `socket_transport` | Ported. Blocking, on a worker. POSIX and Winsock |
| `common/ssl`, `ssl_certificate` | `lib/net/socket_transport` | OpenSSL with system CAs in place of GnuTLS and trust on first use; see [TLS.md](TLS.md) |
| `common/proxy` | none | Not ported: no SOCKS |
| `common/passcrypt`, `password` | `prefs::account_list` | **Partial: passwords are stored Base64 encoded in an owner-only file, not encrypted** |
| `common/tags` | none | Not ported: no tags |
| `common/template`, `prefs_template` | none | Not ported |

## Mail

| Claws | Blaise | Status |
| --- | --- | --- |
| `procheader` | `lib/mime/header`, `lib/mail/msg_info` | Ported |
| `procmime` | `lib/mime/part`, `builder` | Ported for reading and writing; no message/partial reassembly |
| `html`, `enriched` | `lib/mime/html_text` | Ported |
| `procmsg` | `lib/mail/msg_info`, `folder`, `app::engine` | Partial: no message scoring, no return receipts |
| `msgcache` | `lib/mail/msg_cache` | Ported: version 24 cache, version 2 mark file, checked against real Claws. `.claws_tags` not read |
| `folder` | `lib/mail/folder`, `folder_list` | Ported; no virtual or per-folder processing rules |
| `mh` | `lib/mail/mh_folder` | Ported; `.mh_sequences` not written |
| `folderlist.xml` | `lib/mail/folder_list` | Reads Claws' file. Rewrites only what it models, so `order`, `mtime` and per-store sort are lost from Blaise's own copy; `--import-claws` leaves the original alone |
| `mbox`, `exporthtml` | `lib/mail/mbox` | mbox import and export; HTML export not ported |
| `imap`, `etpan/*` | `lib/net/imap_client`, `lib/mail/imap_folder` | Partial: no IDLE, no CONDSTORE, no search on the server, no NAMESPACE, no OAuth2 |
| `news`, `etpan/nntp-thread`, `newsgroup` | none | Not ported: no NNTP |
| `pop`, `inc` | `lib/net/pop3_client`, `app::engine` | Partial: no size limit, no leave-for-N-days, no partial download |
| `common/smtp`, `send_message` | `lib/net/smtp_client`, `app::engine` | Ported: PLAIN, LOGIN, CRAM-MD5. No DIGEST-MD5, OAuth2, sendmail command or POP before SMTP |
| `matcher`, `matcher_parser_*`, `filtering` | `lib/filter` | Partial: no tag, score, test or date conditions; forward, redirect, execute and scoring actions load and save but do not run |
| `advsearch` | `filter::parse_quick_search` | Partial: abbreviations for what `matcher` ports. No "search all folders" |
| `compose` | `lib/compose`, `src/ui/compose_window` | Partial: no spell check, no external editor, no privacy, no inserted files, no per-header entry rows |
| `quote_fmt_*` | `compose::expand_format` | Partial: the % codes; no `?x{}` conditionals |
| `action`, `prefs_actions` | none | Not ported |
| `privacy`, `plugins/pgp*`, `plugins/smime` | none | Not ported: no signing or encryption. Signed mail shows as text with its signature as an attachment |

## Address book

| Claws | Blaise | Status |
| --- | --- | --- |
| `addrbook`, `addritem`, `addrcache`, `addrindex`, `addrgather`, `addrharvest` | `lib/addr/book` | Ported for one kind of book; harvesting not ported |
| `addr_compl` | `addr::book_list::complete` | Ported, with a combo box for the list |
| `ldif`, `exportldif`, `vcard` | `lib/addr/interchange` | Ported |
| `addressbook`, `editaddress`, `editgroup`, `editbook` | `src/ui/settings_windows` | Partial: one address per person in the window, no groups or multiple books in the window |
| `ldapquery`, `ldapserver`, `jpilot`, `addressbook-dbus` | none | Not ported |

## Interface

| Claws | Blaise | Status |
| --- | --- | --- |
| `mainwindow`, `toolbar` | `src/ui/main_window` | The suite's shell in place of Claws'. No layouts, no custom toolbar, no menu shortcuts editor |
| `folderview` | `src/ui/folder_tree` | Partial: no column display, no drag and drop, no context menu |
| `summaryview` | `lib/view/summary`, `src/ui/summary_table` | Partial: threads indent but do not collapse; no color labels shown; no drag and drop |
| `messageview`, `textview` | `lib/view/document`, `src/ui/message_pane` | Partial: no inline images, no faces |
| `mimeview` | `src/ui/message_pane` | Partial: attachments are saved; no viewer or "open with" |
| `prefs_common`, `prefs_*` (about 40 files) | `lib/prefs/common`, `src/ui/settings_windows` | The settings the port acts on |
| `prefs_account`, `account`, `wizard` | `lib/prefs/account`, `src/ui/account_window` | Partial: one form, no wizard. Signatures are text in the account; Claws' signature file or command is not read on import |
| `prefs_filtering`, `prefs_matcher`, `prefs_filtering_action` | `src/ui/settings_windows` | Rules as text, validated |
| `printing` | none | Not ported: Native has no printing |
| `gtk/logwindow` | `src/ui/settings_windows` | Ported |
| `gtk/about` | `main_window::show_about` | Ported |
| `gtk/quicksearch` | The search bar | Ported |
| `gtk/*` other | Native | Replaced |
| `plugins/` (27 directories) | none | Not ported |
| `manual`, `po` | none | Not ported: English only, no manual yet |

## Other limits

- Quitting waits for the job in progress, up to the 60-second network timeout.
- Threads in the list cannot be collapsed: `table_view` groups are flat.
- Claws' `.claws_tags` and per-folder `folderitemrc` settings are left alone, not read.
