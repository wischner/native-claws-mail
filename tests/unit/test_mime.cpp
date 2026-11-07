//
// Codecs, charsets, dates, addresses, headers, encoded words, MIME trees,
// HTML conversion and message rendering.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "check.h"
#include "mime/address.h"
#include "mime/builder.h"
#include "mime/encoded_word.h"
#include "mime/header.h"
#include "mime/html_text.h"
#include "mime/part.h"
#include "util/charset.h"
#include "util/codec.h"
#include "util/date.h"
#include "util/text.h"

namespace {

void test_codecs() {
    CHECK(util::base64_encode("Man") == "TWFu");
    CHECK(util::base64_encode("Ma") == "TWE=");
    CHECK(util::base64_encode("M") == "TQ==");
    CHECK(util::base64_decode("TWFu\r\nTWE=") == "ManMa");
    CHECK(util::qp_encode("caf\xC3\xA9 = x ") == "caf=C3=A9 =3D x=20");
    CHECK(util::qp_decode("caf=C3=A9 =3D x=\r\ny") == "caf\xC3\xA9 = xy");
    CHECK(util::qp_decode("100% =ZZ") == "100% =ZZ");
    CHECK(util::md5_hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(util::md5_hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    // RFC 2195's example.
    CHECK(util::hmac_md5_hex(
              "tanstaaftanstaaf",
              "<1896.697170952@postoffice.reston.mci.net>") ==
          "b913a602c7eda7a495b4e6e7334d3890");
    std::string long_input(120, 'x');
    const std::string encoded = util::qp_encode(long_input);
    CHECK(encoded.find("=\n") != std::string::npos);
    CHECK(util::qp_decode(encoded) == long_input);
}

void test_charsets() {
    CHECK(util::to_utf8("caf\xE9", "iso-8859-1") == "caf\xC3\xA9");
    CHECK(util::to_utf8("\xC3\xA9", "") == "\xC3\xA9");
    CHECK(util::to_utf8("\xE9", "") == "\xC3\xA9");
    CHECK(util::to_utf8("\xE9", "no-such-charset") == "\xC3\xA9");
    CHECK(util::to_utf8("\x80", "windows-1252") == "\xE2\x82\xAC");
    CHECK(util::to_utf8("\xC8", "koi8-r") == "\xD1\x85");
    CHECK(util::from_utf8("\xC3\xA9", "iso-8859-1") == std::string("\xE9"));
    CHECK(!util::from_utf8("\xE2\x82\xAC", "iso-8859-1"));
    CHECK(util::is_valid_utf8("\xC3\xA9"));
    CHECK(!util::is_valid_utf8("\xC3"));
    CHECK(!util::is_valid_utf8("\xC0\x80"));
}

void test_dates() {
    const auto check_date = [](const char *text, std::time_t expected) {
        const auto parsed = util::parse_mail_date(text);
        CHECK(parsed.has_value());
        CHECK(parsed && *parsed == expected);
    };
    check_date("Thu, 13 Feb 1975 23:31:30 +0000", 161566290);
    check_date("13 Feb 1975 23:31:30 GMT", 161566290);
    check_date("Thu, 13 Feb 1975 18:31:30 -0500 (EST)", 161566290);
    check_date("13 Feb 75 23:31 +0000", 161566260);
    check_date("Thu Feb 13 23:31:30 1975", 161566290);
    CHECK(!util::parse_mail_date("yesterday"));
    CHECK(util::format_mail_date(161566290, 60) ==
          "Fri, 14 Feb 1975 00:31:30 +0100");
    CHECK(util::format_mail_date(161566290, -330) ==
          "Thu, 13 Feb 1975 18:01:30 -0530");
}

void test_addresses() {
    auto boxes = mime::parse_addresses(
        "Alice <a@x.org>, \"Bob, Jr.\" <b@x.org>, c@x.org (Carol), d@x.org");
    CHECK(boxes.size() == 4);
    CHECK(boxes[0] == (mime::mailbox{"Alice", "a@x.org"}));
    CHECK(boxes[1] == (mime::mailbox{"Bob, Jr.", "b@x.org"}));
    CHECK(boxes[2] == (mime::mailbox{"Carol", "c@x.org"}));
    CHECK(boxes[3] == (mime::mailbox{"", "d@x.org"}));
    boxes = mime::parse_addresses("team: a@x.org, b@x.org;, c@x.org");
    CHECK(boxes.size() == 3);
    CHECK(boxes[1].address == "b@x.org");
    CHECK(mime::parse_addresses("").empty());
    CHECK(mime::display_name("Alice <a@x.org>") == "Alice");
    CHECK(mime::display_name("a@x.org") == "a@x.org");
    CHECK(mime::format_mailbox({"Bob, Jr.", "b@x.org"}) ==
          "\"Bob, Jr.\" <b@x.org>");
    CHECK(mime::format_mailbox({"", "b@x.org"}) == "b@x.org");
}

void test_headers() {
    std::size_t body = 0;
    const std::string raw = "Subject: one\r\n two\r\nX-A: 1\r\n"
                            "x-a: 2\r\n\r\nbody\r\n";
    const auto block = mime::header_block::parse(raw, &body);
    CHECK(block.get("subject") == "one two");
    CHECK(block.get_all("X-A").size() == 2);
    CHECK(raw.substr(body) == "body\r\n");
    CHECK(mime::header_block::parse("garbage line\n").fields().empty());

    const auto type = mime::parse_parameters(
        "Text/Plain; charset=\"UTF-8\"; name*0=\"ab\"; name*1=\"cd\"");
    CHECK(type.value == "text/plain");
    CHECK(type.parameters.at("charset") == "UTF-8");
    CHECK(type.parameters.at("name") == "abcd");
    const auto encoded = mime::parse_parameters(
        "attachment; filename*=iso-8859-1''caf%E9.txt");
    CHECK(encoded.parameters.at("filename") == "caf\xC3\xA9.txt");
    const std::string folded = mime::fold_header(
        "Subject", std::string(50, 'a') + " " + std::string(50, 'b'));
    CHECK(folded.find("\n ") != std::string::npos);
    const auto again = mime::header_block::parse(folded + "\n\n");
    CHECK(again.get("Subject").size() == 101);
}

void test_encoded_words() {
    CHECK(mime::decode_words("plain") == "plain");
    CHECK(mime::decode_words("=?UTF-8?B?Y2Fmw6k=?=") == "caf\xC3\xA9");
    CHECK(mime::decode_words("=?iso-8859-1?Q?caf=E9_au_lait?=") ==
          "caf\xC3\xA9 au lait");
    CHECK(mime::decode_words("=?UTF-8?Q?a?= =?UTF-8?Q?b?=") == "ab");
    CHECK(mime::decode_words("=?UTF-8?Q?a?= b =?UTF-8?Q?c?=") == "a b c");
    CHECK(mime::decode_words("x =?UTF-8?Q?a?=  y") == "x a  y");
    // A character split across two words must survive.
    CHECK(mime::decode_words("=?UTF-8?B?w?= =?UTF-8?B?w6k=?=").size() > 0);
    CHECK(mime::decode_words("=?UTF-8?Q?=C3?= =?UTF-8?Q?=A9?=") ==
          "\xC3\xA9");
    CHECK(mime::decode_words("=?bogus?X?abc?=") == "=?bogus?X?abc?=");
    CHECK(mime::decode_words("caf\xE9") == "caf\xC3\xA9");
    CHECK(mime::encode_words("plain") == "plain");
    const std::string encoded = mime::encode_words("caf\xC3\xA9 au lait");
    CHECK(encoded.starts_with("=?UTF-8?Q?"));
    CHECK(mime::decode_words(encoded) == "caf\xC3\xA9 au lait");
    std::string big;
    for (int i = 0; i < 40; ++i)
        big += "\xC4\x8D";
    const std::string words = mime::encode_words(big);
    for (const std::string &word : util::split(words, ' '))
        CHECK(word.size() <= 75);
    CHECK(mime::decode_words(words) == big);
}

const char multipart[] =
    "From: a@x.org\r\n"
    "Content-Type: multipart/mixed; boundary=\"XX\"\r\n"
    "\r\n"
    "preamble\r\n"
    "--XX\r\n"
    "Content-Type: multipart/alternative; boundary=\"YY\"\r\n"
    "\r\n"
    "--YY\r\n"
    "Content-Type: text/plain; charset=iso-8859-1\r\n"
    "Content-Transfer-Encoding: quoted-printable\r\n"
    "\r\n"
    "caf=E9\r\n"
    "--YY\r\n"
    "Content-Type: text/html\r\n"
    "\r\n"
    "<b>cafe</b>\r\n"
    "--YY--\r\n"
    "--XX\r\n"
    "Content-Type: application/pdf; name=\"a.pdf\"\r\n"
    "Content-Disposition: attachment; filename=\"b.pdf\"\r\n"
    "Content-Transfer-Encoding: base64\r\n"
    "\r\n"
    "TWFu\r\n"
    "--XX--\r\n"
    "epilogue\r\n";

void test_parts() {
    const std::string raw = multipart;
    const mime::part root = mime::parse_message(raw);
    CHECK(root.is_multipart());
    CHECK(root.children.size() == 2);
    CHECK(root.children[0].children.size() == 2);
    const mime::part *body = mime::body_part(root);
    CHECK(body != nullptr && body->subtype == "plain");
    CHECK(mime::body_text(root, raw) == "caf\xC3\xA9");
    const auto files = mime::attachments(root);
    CHECK(files.size() == 1);
    CHECK(files[0]->filename == "b.pdf");
    CHECK(files[0]->decoded_body(raw) == "Man");

    // No closing boundary, and a boundary that never appears.
    const std::string cut =
        "Content-Type: multipart/mixed; boundary=Q\n\n--Q\n\nhello\n";
    const mime::part open_root = mime::parse_message(cut);
    CHECK(open_root.children.size() == 1);
    CHECK(mime::body_text(open_root, cut) == "hello\n");
    const std::string none =
        "Content-Type: multipart/mixed; boundary=Q\n\nno parts\n";
    CHECK(mime::parse_message(none).children.empty());
    CHECK(mime::parse_message("").type == "text");

    // Deep nesting stops rather than overflowing.
    std::string deep;
    for (int i = 0; i < 2000; ++i)
        deep += "Content-Type: message/rfc822\n\n";
    deep += "x";
    CHECK(mime::parse_message(deep).type == "message");

    const std::string html = "Content-Type: text/html\n\n<p>a</p><p>b</p>";
    CHECK(mime::body_text(mime::parse_message(html), html) == "a\n\nb\n");

    const std::string uu = "Content-Transfer-Encoding: x-uuencode\n\n"
                           "begin 644 x\n#0V%T\n`\nend\n";
    const mime::part encoded = mime::parse_message(uu);
    CHECK(encoded.decoded_body(uu) == "Cat");
}

void test_html() {
    using mime::html_to_text;
    CHECK(html_to_text("<html><head><title>x</title>"
                       "<style>p{}</style></head><body>hi</body></html>") ==
          "hi\n");
    CHECK(html_to_text("a<br>b") == "a\nb\n");
    CHECK(html_to_text("<ul><li>one<li>two</ul>") ==
          "\xE2\x80\xA2 one\n\xE2\x80\xA2 two\n");
    CHECK(html_to_text("x &amp; y &#65; &#x42; &nope;") ==
          "x & y A B &nope;\n");
    CHECK(html_to_text("<a href=\"http://x.org/\">site</a>") ==
          "site <http://x.org/>\n");
    CHECK(html_to_text("<a href=\"http://x.org/\">http://x.org/</a>") ==
          "http://x.org/\n");
    CHECK(html_to_text("<script>alert(1)</script>ok") == "ok\n");
    CHECK(html_to_text("<pre>a\n  b</pre>") == "a\n  b\n");
    CHECK(html_to_text("a    b\n\nc") == "a b c\n");
    CHECK(html_to_text("<img src=\"http://track/\" alt=\"logo\">") ==
          "[logo]\n");
    CHECK(mime::enriched_to_text("<bold>hi</bold>\nthere\n\nnext") ==
          "hi there\nnext");
}

void test_builder() {
    mime::outgoing_message message;
    message.from = "Alice <a@x.org>";
    message.to = "Bob <b@x.org>, \xC4\x8Crt <c@x.org>";
    message.bcc = "secret@x.org";
    message.subject = "caf\xC3\xA9";
    message.body = "line one\ncaf\xC3\xA9\n";
    message.date = 161566290;
    message.message_id = "1@x.org";
    message.attachments.push_back({"a.bin", "", "Man"});
    std::string raw = mime::render_message(message, "BB");
    CHECK(raw.find("\r\n") != std::string::npos);
    CHECK(raw.find("secret") == std::string::npos);
    CHECK(raw.find("\nFrom: Alice <a@x.org>\r\n") != std::string::npos);
    const auto block = mime::header_block::parse(raw);
    CHECK(mime::decode_words(block.get("Subject")) == "caf\xC3\xA9");
    CHECK(mime::decode_words(block.get("To")).find("\xC4\x8Crt") !=
          std::string::npos);
    const mime::part root = mime::parse_message(raw);
    CHECK(root.children.size() == 2);
    CHECK(mime::body_text(root, raw) == "line one\ncaf\xC3\xA9\n");
    CHECK(root.children[1].filename == "a.bin");
    CHECK(root.children[1].decoded_body(raw) == "Man");

    mime::outgoing_message plain;
    plain.from = "a@x.org";
    plain.to = "b@x.org";
    plain.subject = "hi";
    plain.body = "From here\n" + std::string(1200, 'x') + "\n";
    plain.date = 1;
    raw = mime::render_message(plain);
    for (const std::string &line : util::split_lines(raw))
        CHECK(line.size() <= 998);
    CHECK(mime::body_text(mime::parse_message(raw), raw) == plain.body);
    CHECK(mime::guess_mime_type("x.PNG") == "image/png");
    CHECK(mime::guess_mime_type("x.nope") == "application/octet-stream");
    CHECK(mime::make_message_id("Al <a@x.org>", 5).ends_with("@x.org"));
}

} // namespace

int main() {
    test_codecs();
    test_charsets();
    test_dates();
    test_addresses();
    test_headers();
    test_encoded_words();
    test_parts();
    test_html();
    test_builder();
    return testing::summary("mime");
}
