#pragma once

#include "../../ilias.hpp"
#include "../../detail/expected.hpp"
#include "huffman.hpp"
#include "integer.hpp"

#include <vector>
#include <string>
#include <optional>

ILIAS_NS_BEGIN
namespace http2::detail {

template <typename ValueT, std::size_t N = 256>
class DictionaryTree {
    struct Node {
        unsigned char                      key;
        std::optional<ValueT>              value;
        std::vector<std::unique_ptr<Node>> children;

        Node &operator=(const Node &) = delete;
        Node &operator=(Node &&other) {
            key      = other.key;
            value    = std::move(other.value);
            children = std::move(other.children);
            return *this;
        }

        bool    isLeaf() const noexcept { return children.empty(); }
        bool    hasValue() const noexcept { return value.has_value(); }
        ValueT  operator*() const noexcept { return value.value(); }
        ValueT &operator*() noexcept { return value.value(); }
        Node   *find(unsigned char i) const noexcept {
              auto it = std::lower_bound(children.begin(), children.end(), i,
                                         [](const std::unique_ptr<Node> &a, unsigned char b) { return a->key < b; });
              if (it != children.end() && (*it)->key == i) {
                  return it->get();
            }
              return nullptr;
        }
        Node *insert(const unsigned char key, std::optional<ValueT> &&value) noexcept {
            auto it = std::lower_bound(children.begin(), children.end(), key,
                                       [](const std::unique_ptr<Node> &a, unsigned char b) { return a->key < b; });
            if (it != children.end() && (*it)->key == key) {
                return it->get();
            }
            return children.insert(it, std::make_unique<Node>(key, value))->get();
        }
        void erase(unsigned char i) noexcept {
            auto it = std::lower_bound(children.begin(), children.end(), i,
                                       [](const std::unique_ptr<Node> &a, unsigned char b) { return a->key < b; });
            if (it != children.end() && (*it)->key == i) {
                children.erase(it);
            }
        }
    };

public:
    using value_type = ValueT;

    DictionaryTree() = default;

    DictionaryTree(const DictionaryTree &) = delete;
    DictionaryTree(DictionaryTree &&other) {
        mRoot       = std::move(other.mRoot);
        other.mRoot = Node {};
        mSize       = other.mSize;
    }

    DictionaryTree &operator=(const DictionaryTree &) = delete;
    DictionaryTree &operator=(DictionaryTree &&other) {
        mRoot       = std::move(other.mRoot);
        other.mRoot = Node {};
        mSize       = other.mSize;
        return *this;
    }

    void insert(std::string_view key, const value_type &value) {
        Node *node = &mRoot;
        for (const auto c : key) {
            node = node->insert(c, std::nullopt);
        }
        if (!node->hasValue()) {
            mSize++;
        }
        node->value = value;
    }
    std::optional<value_type> find(std::string_view key) const noexcept {
        auto node = &mRoot;
        for (const auto c : key) {
            node = node->find(c);
            if (!node) {
                return std::nullopt;
            }
        }
        return node->value;
    }
    void remove(std::string_view key) noexcept {
        Node         *node = &mRoot, *parent = &mRoot;
        unsigned char removed_key = key[0];
        for (const auto c : key) {
            auto node1 = node->find(c);
            // if node has more than one child or has value, save it
            // o is empty node, x is node with value, r is removed node
            // like o-o-x-o-x-r, need to remove r only
            // like o-o-x-o-o-o-r, need to remove o-o-o-r
            // like o-o-o-r-x, can not remove r, only set r to empty node
            // like o-o-o-o-r
            //          \-o-x, need to remove o-r
            if (!node->isLeaf() && (node->children.size() > 1 || node->hasValue())) {
                parent      = node;
                removed_key = c;
            }
            if (!node1) {
                return;
            }
            node = node1;
        }
        --mSize;
        if (node->isLeaf()) {
            parent->erase(removed_key);
        }
        else {
            node->value.reset();
        }
    }
    void clear() noexcept {
        mRoot.children.clear();
        mSize = 0;
    }
    std::size_t size() const noexcept { return mSize; }

private:
    Node        mRoot = {};
    std::size_t mSize = {};
};

template <typename ValueT>
class DictionaryTree<ValueT, 2> {
    struct Node {
        unsigned char           key      = -1;
        std::optional<ValueT>   value    = {};
        std::unique_ptr<Node[]> children = nullptr;

        Node &operator=(const Node &) = delete;
        Node &operator=(Node &&other) {
            key      = other.key;
            value    = std::move(other.value);
            children = std::move(other.children);
            return *this;
        }

        bool    isLeaf() const noexcept { return nullptr == children; }
        bool    hasValue() const noexcept { return value.has_value(); }
        ValueT  operator*() const noexcept { return value.value(); }
        ValueT &operator*() noexcept { return value.value(); }
        Node   *find(unsigned char i) const noexcept {
              ILIAS_ASSERT(i < 2);
              if (children && children[i].key == i) {
                  return &children[i];
            }
              return nullptr;
        }
        Node *insert(const unsigned char key) noexcept {
            ILIAS_ASSERT(key < 2);
            if (!children) {
                children = std::make_unique<Node[]>(2);
            }
            children[key].key = key;
            return &children[key];
        }
        void erase(unsigned char i) noexcept {
            ILIAS_ASSERT(i < 2);
            children[i].value.reset();
            children[i].children.reset();
        }
    };

public:
    using value_type = ValueT;

    DictionaryTree() = default;

    DictionaryTree(const DictionaryTree &) = delete;
    DictionaryTree(DictionaryTree &&other) {
        mRoot       = std::move(other.mRoot);
        other.mRoot = Node {};
        mSize       = other.mSize;
    }

    DictionaryTree &operator=(const DictionaryTree &) = delete;
    DictionaryTree &operator=(DictionaryTree &&other) {
        mRoot       = std::move(other.mRoot);
        other.mRoot = Node {};
        mSize       = other.mSize;
        return *this;
    }
    void setZero(const unsigned char zero) noexcept { mZero = zero; }
    void insert(std::string_view key, const value_type &value) {
        Node *node = &mRoot;
        for (const auto c : key) {
            node = node->insert(c != mZero);
        }
        if (!node->hasValue()) {
            mSize++;
        }
        node->value = value;
    }
    template <typename T, std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, char>::type = '0'>
    void insert(T key, const value_type &value, std::size_t bitsLenght = -1) {
        Node *node = &mRoot;
        if (bitsLenght == -1) {
            bitsLenght = sizeof(T) * 8;
        }
        ILIAS_ASSERT(bitsLenght <= sizeof(T) * 8);
        for (int i = bitsLenght - 1; i >= 0; --i) {
            node = node->insert(((key >> i) & 1U) != 0);
        }
        if (!node->hasValue()) {
            mSize++;
        }
        node->value = value;
    }
    std::optional<value_type> find(std::string_view key) const noexcept {
        auto node = &mRoot;
        for (const auto c : key) {
            node = node->find(c != mZero);
            if (!node) {
                return std::nullopt;
            }
        }
        return node->value;
    }
    template <typename T, std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, char>::type = '0'>
    std::optional<value_type> find(T key, std::size_t bitsLenght = -1) const noexcept {
        auto node = &mRoot;
        if (bitsLenght == -1) {
            bitsLenght = sizeof(T) * 8;
        }
        ILIAS_ASSERT(bitsLenght <= sizeof(T) * 8);
        for (int i = bitsLenght - 1; i >= 0; --i) {
            node = node->find(((key >> i) & 1U) != 0);
            if (!node) {
                return std::nullopt;
            }
        }
        return node->value;
    }
    void remove(std::string_view key) noexcept {
        Node         *node = &mRoot, *parent = &mRoot;
        unsigned char removed_key = key[0];
        for (const auto c : key) {
            auto node1 = node->find(c != mZero);
            // not leaf, has value or has two children node is not a single branched, so can not be removed
            if (!node->isLeaf() && ((node->children[0].key != 0 && node->children[1].key != 0) || node->hasValue())) {
                parent      = node;
                removed_key = c;
            }
            if (!node1) {
                return;
            }
            node = node1;
        }
        --mSize;
        // if remove node is not a leaf, remove the value but keep the node for the branch
        if (node->isLeaf()) {
            parent->erase(removed_key != mZero);
        }
        else {
            node->value.reset();
        }
    }
    template <typename T, std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, char>::type = '0'>
    void remove(T key, std::size_t bitsLenght = -1) noexcept {
        Node *node = &mRoot, *parent = &mRoot;
        if (bitsLenght == -1) {
            bitsLenght = sizeof(T) * 8;
        }
        ILIAS_ASSERT(bitsLenght <= sizeof(T) * 8);
        unsigned char removed_key = ((key >> (bitsLenght - 1)) & 1) != 0;
        for (int i = bitsLenght - 1; i >= 0; --i) {
            auto node1 = node->find(((key >> i) & 1U) != 0);
            // not leaf, has value or has two children node is not a single branched, so can not be removed
            if (!node->isLeaf() && ((node->children[0].key != 0 && node->children[1].key != 0) || node->hasValue())) {
                parent      = node;
                removed_key = (((key >> i) & 1U) != 0);
            }
            if (!node1) {
                return;
            }
            node = node1;
        }
        --mSize;
        // if remove node is not a leaf, remove the value but keep the node for the branch
        if (node->isLeaf()) {
            parent->erase(removed_key);
        }
        else {
            node->value.reset();
        }
    }
    void clear() noexcept {
        mRoot.children.reset();
        mSize = 0;
    }
    std::size_t size() const noexcept { return mSize; }

private:
    Node          mRoot = {static_cast<unsigned char>(-1), std::nullopt, nullptr};
    std::size_t   mSize = {};
    unsigned char mZero = '0';
};

} // namespace http2::detail

ILIAS_NS_END