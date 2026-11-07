//
// folderlist.xml.
//
// Derived from Claws Mail src/folder.c (folder_read_list,
// folder_item_get_xml),
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mail/folder_list.h"

#include <array>

#include "mail/mh_folder.h"
#include "util/files.h"
#include "util/text.h"
#include "util/xml_reader.h"

namespace mail {
namespace {

namespace fs = std::filesystem;

constexpr std::array<const char *, 6> type_names{
    "normal", "inbox", "outbox", "draft", "queue", "trash"};
constexpr std::array<const char *, 15> sort_names{
    "none",  "number", "size",   "date", "from",
    "subject", "score", "label", "mark", "unread",
    "mime",  "to",     "locked", "tags", "thread_date"};

// Return an attribute, or empty.
std::string attribute(const util::xml_attributes &attributes,
                      std::string_view name) {
    const auto it = attributes.find(name);
    return it == attributes.end() ? std::string() : it->second;
}

// Return an integer attribute.
int number_attribute(const util::xml_attributes &attributes,
                     std::string_view name) {
    return static_cast<int>(
        util::parse_int(attribute(attributes, name)).value_or(0));
}

// Apply the saved attributes of an item.
void apply_attributes(folder_item &item,
                      const util::xml_attributes &attributes) {
    const std::string type = attribute(attributes, "type");
    for (std::size_t i = 0; i < type_names.size(); ++i)
        if (type == type_names[i])
            item.type = static_cast<item_type>(i);
    const std::string sort = attribute(attributes, "sort_key");
    for (std::size_t i = 0; i < sort_names.size(); ++i)
        if (sort == sort_names[i])
            item.sort = static_cast<sort_key>(i);
    item.sort_ascending =
        attribute(attributes, "sort_type") != "descending";
    item.collapsed = attribute(attributes, "collapsed") == "1";
    item.threaded = attribute(attributes, "threaded") != "0";
    item.hide_read = attribute(attributes, "hidereadmsgs") == "1";
    item.no_select = attribute(attributes, "no_select") == "1";
    item.no_sub = attribute(attributes, "no_sub") == "1";
    item.account_id = number_attribute(attributes, "account_id");
    item.new_count = number_attribute(attributes, "new");
    item.unread_count = number_attribute(attributes, "unread");
    item.marked_count = number_attribute(attributes, "marked");
    item.total_count = number_attribute(attributes, "total");
}

// Builds stores as the document streams past.
class list_reader final : public util::xml_handler {
public:
    list_reader(folder_list &list, folder_factory *factory)
        : list_(list), factory_(factory) {}

    void start_element(std::string_view name,
                       const util::xml_attributes &attributes) override {
        if (name == "folder") {
            open_store(attributes);
        } else if (name == "folderitem" && store_ != nullptr) {
            open_item(attributes);
        }
    }

    void end_element(std::string_view name) override {
        if (name == "folderitem" && !stack_.empty())
            stack_.pop_back();
        if (name == "folder") {
            store_ = nullptr;
            stack_.clear();
        }
    }

    void characters(std::string_view) override {}

private:
    void open_store(const util::xml_attributes &attributes) {
        const std::string type = attribute(attributes, "type");
        const std::string name = attribute(attributes, "name");
        const fs::path path = attribute(attributes, "path");
        std::unique_ptr<folder> store;
        if (type == "mh")
            store = std::make_unique<mh_folder>(name, path);
        else if (factory_ != nullptr)
            store = factory_->create(
                type, name, number_attribute(attributes, "account_id"),
                path);
        store_ = store ? &list_.add(std::move(store)) : nullptr;
    }

    void open_item(const util::xml_attributes &attributes) {
        const std::string path = attribute(attributes, "path");
        folder_item *item = path.empty() ? &store_->root()
                                         : store_->find_item(path);
        if (item == nullptr && !stack_.empty()) {
            auto made = std::make_unique<folder_item>(
                *store_, stack_.back(), attribute(attributes, "name"),
                path);
            stack_.back()->children.push_back(std::move(made));
            item = stack_.back()->children.back().get();
        }
        if (item != nullptr && !path.empty())
            apply_attributes(*item, attributes);
        stack_.push_back(item != nullptr ? item : &store_->root());
    }

    folder_list &list_;
    folder_factory *factory_;
    folder *store_ = nullptr;
    std::vector<folder_item *> stack_;
};

// Append an attribute.
void put(std::string &out, const char *name, std::string_view value) {
    out += ' ';
    out += name;
    out += "=\"";
    out += util::xml_escape(value);
    out += '"';
}

// Append one item and its children.
void write_item(std::string &out, const folder_item &item, int depth) {
    out.append(static_cast<std::size_t>(depth) * 4, ' ');
    out += "<folderitem";
    put(out, "type", type_names[static_cast<std::size_t>(item.type)]);
    put(out, "name", item.name);
    if (!item.path.empty())
        put(out, "path", item.path);
    if (item.no_sub)
        put(out, "no_sub", "1");
    if (item.no_select)
        put(out, "no_select", "1");
    put(out, "collapsed", item.collapsed ? "1" : "0");
    put(out, "threaded", item.threaded ? "1" : "0");
    put(out, "hidereadmsgs", item.hide_read ? "1" : "0");
    put(out, "sort_key", sort_names[static_cast<std::size_t>(item.sort)]);
    put(out, "sort_type", item.sort_ascending ? "ascending" : "descending");
    put(out, "new", std::to_string(item.new_count));
    put(out, "unread", std::to_string(item.unread_count));
    put(out, "marked", std::to_string(item.marked_count));
    put(out, "total", std::to_string(item.total_count));
    if (item.account_id != 0)
        put(out, "account_id", std::to_string(item.account_id));
    if (item.children.empty()) {
        out += "/>\n";
        return;
    }
    out += ">\n";
    for (const auto &child : item.children)
        write_item(out, *child, depth + 1);
    out.append(static_cast<std::size_t>(depth) * 4, ' ');
    out += "</folderitem>\n";
}

} // namespace

bool folder_list::load(const fs::path &path, folder_factory *factory) {
    const auto text = util::read_file(path);
    if (!text)
        return false;
    list_reader reader(*this, factory);
    std::string error;
    return util::parse_xml(*text, reader, error);
}

bool folder_list::save(const fs::path &path) const {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<folderlist config_version=\"4\">\n";
    for (const auto &store : folders_) {
        out += "    <folder";
        put(out, "type", store->type_name());
        put(out, "name", store->name());
        if (const auto *mh = dynamic_cast<mh_folder *>(store.get()))
            put(out, "path", mh->directory().string());
        if (store->account_id() != 0)
            put(out, "account_id", std::to_string(store->account_id()));
        out += ">\n";
        for (const auto &child : store->root().children)
            write_item(out, *child, 2);
        out += "    </folder>\n";
    }
    out += "</folderlist>\n";
    return util::write_file(path, out);
}

folder &folder_list::add(std::unique_ptr<folder> store) {
    folders_.push_back(std::move(store));
    return *folders_.back();
}

void folder_list::remove(folder &store) {
    std::erase_if(folders_, [&](const auto &item) {
        return item.get() == &store;
    });
}

folder_item *folder_list::find(std::string_view identifier) const {
    for (const auto &store : folders_) {
        std::string prefix = "#";
        prefix += store->type_name();
        prefix += "/" + store->name();
        if (identifier == prefix)
            return &store->root();
        if (identifier.starts_with(prefix + "/"))
            return store->find_item(identifier.substr(prefix.size() + 1));
    }
    return nullptr;
}

folder_item *folder_list::special_for(const folder_item *item,
                                      item_type type) const {
    if (item != nullptr)
        if (folder_item *found = item->owner().find_special(type))
            return found;
    for (const auto &store : folders_)
        if (folder_item *found = store->find_special(type))
            return found;
    return nullptr;
}

} // namespace mail
