//
// The IMAP store.
//
// Derived from Claws Mail src/imap.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mail/imap_folder.h"

#include <algorithm>
#include <array>

#include "util/files.h"
#include "util/log.h"
#include "util/text.h"

namespace mail {
namespace {

namespace fs = std::filesystem;

constexpr const char *validity_file = ".blaise_uidvalidity";
constexpr std::size_t fetch_batch = 200;

// A permanent flag and the server flag that mirrors it.
struct flag_pair {
    std::uint32_t local;
    const char *remote;
};

constexpr std::array<flag_pair, 4> mirrored{{
    {flag_marked, "\\Flagged"},
    {flag_replied, "\\Answered"},
    {flag_forwarded, "$Forwarded"},
    {flag_deleted, "\\Deleted"},
}};

// Return a flag's special folder type from its conventional name.
item_type guess_type(std::string_view name) {
    const std::string lowered = util::lower(name);
    if (lowered == "inbox")
        return item_type::inbox;
    if (lowered == "sent" || lowered == "sent items" ||
        lowered == "sent messages" || lowered == "sent mail")
        return item_type::outbox;
    if (lowered == "drafts" || lowered == "draft")
        return item_type::draft;
    if (lowered == "trash" || lowered == "deleted items" ||
        lowered == "deleted messages")
        return item_type::trash;
    if (lowered == "queue")
        return item_type::queue;
    return item_type::normal;
}

// Turn UIDs into comma separated batches an IMAP command can carry.
std::vector<std::string>
uid_batches(const std::vector<std::uint32_t> &uids) {
    std::vector<std::string> batches;
    for (std::size_t i = 0; i < uids.size(); i += fetch_batch) {
        std::vector<std::string> part;
        for (std::size_t k = i; k < std::min(uids.size(), i + fetch_batch);
             ++k)
            part.push_back(std::to_string(uids[k]));
        batches.push_back(util::join(part, ","));
    }
    return batches;
}

} // namespace

std::uint32_t flags_from_imap(const std::set<std::string> &flags,
                              std::uint32_t local) {
    std::uint32_t result = local;
    for (const flag_pair &pair : mirrored) {
        if (flags.contains(pair.remote))
            result |= pair.local;
        else
            result &= ~pair.local;
    }
    if (flags.contains("\\Seen"))
        result &= ~(flag_new | flag_unread);
    else
        result |= flag_unread;
    return result;
}

imap_folder::imap_folder(std::string name, imap_settings settings,
                         fs::path cache)
    : folder(std::move(name)), settings_(std::move(settings)),
      cache_(std::move(cache)) {}

void imap_folder::update_settings(imap_settings settings) {
    settings_ = std::move(settings);
    drop_session();
}

net::imap_client &imap_folder::session() {
    if (client_)
        return *client_;
    if (!settings_.connect)
        throw net::net_error("This account cannot connect.");
    auto client = std::make_unique<net::imap_client>(settings_.server,
                                                     settings_.connect);
    if (!client->authenticated())
        client->login(settings_.user, settings_.password);
    client_ = std::move(client);
    return *client_;
}

void imap_folder::drop_session() {
    client_.reset();
    selected_ = nullptr;
}

// Servers time out idle sessions. A dead session is only discovered by
// using it, so a network failure gets one reconnection; a refused login
// is not retried, since repeating it would lock the account.
template <typename Call>
auto imap_folder::with_retry(Call &&call) {
    try {
        return call();
    } catch (const net::auth_error &) {
        throw;
    } catch (const net::net_error &failure) {
        util::log_write(util::log_level::warning, failure.what());
        drop_session();
        return call();
    }
}

std::string imap_folder::mailbox_name(const folder_item &item) const {
    const auto known = raw_names_.find(item.path);
    if (known != raw_names_.end())
        return known->second;
    std::string raw = net::imap_utf7_encode(item.path);
    if (delimiter_ != '/')
        std::ranges::replace(raw, '/', delimiter_);
    return raw;
}

fs::path imap_folder::cache_directory(const folder_item &item) const {
    return item.path.empty() ? cache_ : cache_ / item.path;
}

fs::path imap_folder::message_path(const folder_item &item,
                                   std::uint32_t number) const {
    return cache_directory(item) / std::to_string(number);
}

void imap_folder::select(folder_item &item) {
    if (selected_ == &item)
        return;
    const net::imap_selection selection =
        session().select(mailbox_name(item));
    selected_ = &item;
    const fs::path stamp = cache_directory(item) / validity_file;
    const std::string validity = std::to_string(selection.uid_validity);
    const auto stored = util::read_file(stamp);
    if (stored && *stored != validity) {
        // Every UID changed meaning, so every cached message is suspect.
        util::log_write(util::log_level::warning,
                        "UIDVALIDITY changed; refetching " + item.path);
        std::error_code ignored;
        for (const auto &entry :
             fs::directory_iterator(cache_directory(item), ignored))
            if (entry.is_regular_file(ignored))
                fs::remove(entry.path(), ignored);
    }
    if (!stored || *stored != validity)
        util::write_file(stamp, validity);
}

std::vector<std::uint32_t> imap_folder::list_numbers(folder_item &item) {
    return with_retry([&] {
        select(item);
        pending_.clear();
        remote_flags_.clear();
        const std::vector<std::uint32_t> uids = session().search_all();
        const msg_map &known = item.loaded() ? item.messages() : msg_map{};
        std::vector<std::uint32_t> fresh;
        for (const std::uint32_t uid : uids)
            if (!known.contains(uid))
                fresh.push_back(uid);
        for (const std::string &batch : uid_batches(fresh))
            for (net::imap_message &message :
                 session().fetch_summaries(batch, true))
                pending_[message.uid] = std::move(message);
        if (!uids.empty())
            for (const net::imap_message &message :
                 session().fetch_summaries("1:*", false)) {
                const auto it = known.find(message.uid);
                const std::uint32_t local =
                    it == known.end() ? 0 : it->second.perm_flags;
                remote_flags_[message.uid] =
                    flags_from_imap(message.flags, local);
            }
        return uids;
    });
}

std::optional<msg_info> imap_folder::read_info(folder_item &,
                                               std::uint32_t number) {
    const auto it = pending_.find(number);
    if (it == pending_.end())
        return std::nullopt;
    msg_info info = parse_msg_info(it->second.headers, number);
    info.size = it->second.size;
    info.perm_flags = flags_from_imap(it->second.flags, 0);
    if (info.has(flag_unread))
        info.perm_flags |= flag_new;
    return info;
}

void imap_folder::scan_finished(folder_item &item) {
    for (const auto &[uid, flags] : remote_flags_)
        item.apply_store_flags(uid, flags);
    remote_flags_.clear();
    pending_.clear();
}

std::optional<std::string> imap_folder::fetch(folder_item &item,
                                              std::uint32_t number) {
    const fs::path path = message_path(item, number);
    if (auto cached = util::read_file(path))
        return cached;
    auto body = with_retry([&] {
        select(item);
        return session().fetch_message(number);
    });
    if (body)
        util::write_file(path, *body);
    return body;
}

std::uint32_t imap_folder::add(folder_item &item, std::string_view raw,
                               std::uint32_t perm_flags) {
    std::vector<std::string> flags;
    if ((perm_flags & flag_unread) == 0)
        flags.emplace_back("\\Seen");
    for (const flag_pair &pair : mirrored)
        if ((perm_flags & pair.local) != 0)
            flags.emplace_back(pair.remote);
    return with_retry([&]() -> std::uint32_t {
        const std::uint32_t uid = session().append(
            mailbox_name(item), raw, util::join(flags, " "));
        if (uid != 0)
            return uid;
        // No UIDPLUS: the new message is the highest UID.
        selected_ = nullptr;
        select(item);
        const auto uids = session().search_all();
        return uids.empty() ? 0 : uids.back();
    });
}

bool imap_folder::remove(folder_item &item, std::uint32_t number) {
    with_retry([&] {
        select(item);
        session().store_flags(number, true, "\\Deleted");
        session().expunge(number);
    });
    std::error_code ignored;
    fs::remove(message_path(item, number), ignored);
    return true;
}

std::uint32_t imap_folder::server_copy(folder_item &from, folder_item &to,
                                       std::uint32_t number) {
    return with_retry([&] {
        select(from);
        return session().copy(number, mailbox_name(to));
    });
}

void imap_folder::flags_changed(folder_item &item, std::uint32_t number,
                                std::uint32_t old_flags,
                                std::uint32_t new_flags) {
    const std::uint32_t changed = old_flags ^ new_flags;
    with_retry([&] {
        select(item);
        if ((changed & flag_unread) != 0)
            session().store_flags(number, (new_flags & flag_unread) == 0,
                                  "\\Seen");
        for (const flag_pair &pair : mirrored)
            if ((changed & pair.local) != 0)
                session().store_flags(number,
                                      (new_flags & pair.local) != 0,
                                      pair.remote);
    });
}

folder_item *imap_folder::create_folder(folder_item &parent,
                                        const std::string &name) {
    if (name.empty() || name.find(delimiter_) != std::string::npos ||
        name.find('/') != std::string::npos)
        return nullptr;
    const std::string path =
        parent.path.empty() ? name : parent.path + "/" + name;
    std::string raw = settings_.directory + net::imap_utf7_encode(path);
    if (delimiter_ != '/')
        std::ranges::replace(raw, '/', delimiter_);
    with_retry([&] { session().create(raw); });
    raw_names_[path] = raw;
    parent.children.push_back(
        std::make_unique<folder_item>(*this, &parent, name, path));
    return parent.children.back().get();
}

bool imap_folder::rename_folder(folder_item &item,
                                const std::string &name) {
    if (item.parent() == nullptr || name.empty())
        return false;
    const std::string old_raw = mailbox_name(item);
    const folder_item *parent = item.parent();
    const std::string path =
        parent->path.empty() ? name : parent->path + "/" + name;
    std::string raw = settings_.directory + net::imap_utf7_encode(path);
    if (delimiter_ != '/')
        std::ranges::replace(raw, '/', delimiter_);
    with_retry([&] {
        selected_ = nullptr;
        session().rename(old_raw, raw);
    });
    std::error_code ignored;
    fs::rename(cache_directory(item), cache_ / path, ignored);
    raw_names_.erase(item.path);
    raw_names_[path] = raw;
    item.name = name;
    item.set_path(path);
    return true;
}

bool imap_folder::remove_folder(folder_item &item) {
    if (item.parent() == nullptr)
        return false;
    const std::string raw = mailbox_name(item);
    with_retry([&] {
        selected_ = nullptr;
        session().remove(raw);
    });
    std::error_code ignored;
    fs::remove_all(cache_directory(item), ignored);
    raw_names_.erase(item.path);
    detach_item(item);
    return true;
}

void imap_folder::scan_tree() {
    const auto boxes = with_retry([&] {
        return session().list(settings_.directory, "*",
                              settings_.subscribed_only);
    });
    if (!boxes.empty())
        delimiter_ = boxes.front().delimiter;
    bool seen_inbox = false;
    for (const net::imap_mailbox &box : boxes) {
        seen_inbox |= util::iequals(box.raw_name, "INBOX");
        std::string decoded = box.name;
        if (!settings_.directory.empty() &&
            decoded.starts_with(settings_.directory))
            decoded.erase(0, settings_.directory.size());
        if (decoded.empty())
            continue;
        std::ranges::replace(decoded, box.delimiter, '/');
        folder_item *parent = &root();
        const auto parts = util::split(decoded, '/');
        std::string path;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            path += (i == 0 ? "" : "/") + parts[i];
            folder_item *next = find_item(path);
            if (next == nullptr) {
                const bool top = i == 0;
                parent->children.push_back(std::make_unique<folder_item>(
                    *this, parent, parts[i], path,
                    top ? guess_type(parts[i]) : item_type::normal));
                next = parent->children.back().get();
                next->no_select = i + 1 < parts.size();
            }
            parent = next;
        }
        parent->no_select = box.no_select;
        parent->no_sub = box.no_inferiors;
        raw_names_[path] = box.raw_name;
    }
    if (!seen_inbox && settings_.subscribed_only &&
        find_item("INBOX") == nullptr) {
        // INBOX always exists even when it is not subscribed.
        root().children.insert(
            root().children.begin(),
            std::make_unique<folder_item>(*this, &root(), "INBOX", "INBOX",
                                          item_type::inbox));
        raw_names_["INBOX"] = "INBOX";
    }
    std::ranges::stable_sort(
        root().children, {}, [](const std::unique_ptr<folder_item> &item) {
            return item->type == item_type::normal
                       ? 99
                       : static_cast<int>(item->type);
        });
}

} // namespace mail
