//
// Created by jamal on 02/12/2025.
//

#include <stdexcept>
#include "ds/avl.h"

using std::make_shared;
using std::shared_ptr;
using std::pair;

template<typename TKey, typename TValue>
int Avl<TKey, TValue>::AvlNode::heightOf(const shared_ptr<AvlNode> &node) {
    return node == nullptr ? 0 : node->height;
}

template<typename TKey, typename TValue>
void Avl<TKey, TValue>::AvlNode::update() {
    auto hl = heightOf(left);
    auto hr = heightOf(right);

    // this would lead to that the new balance for the old head to be node->left->balance - node->right->head. all which already computed
    balance = hl - hr;
    // for the height, these subtrees were untouched, therefore, they are "invariants" meaning their numbers are correct
    height = std::max(hl, hr) + 1;
}

template<typename TKey, typename TValue>
std::shared_ptr<typename Avl<TKey, TValue>::AvlNode> Avl<TKey, TValue>::rotateLeft(std::shared_ptr<AvlNode> node) {
    if (node == nullptr) {
        throw std::invalid_argument("rotate called on null tree!");
    }
    if (node->right == nullptr) {
        throw std::invalid_argument("rotate called on right null tree (no where to rotate)!");
    }
    // after rotation to the left, the new head is the node's right.
    auto newHead = node->right;

    // current head's right is the new's left (since node < newHead->right < newHead)
    node->right = newHead->left;
    // then the left of the new head is the current head.
    newHead->left = node;

    node->update();

    newHead->update();

    return newHead;
}

template<typename TKey, typename TValue>
std::shared_ptr<typename Avl<TKey, TValue>::AvlNode> Avl<TKey, TValue>::rotateRight(std::shared_ptr<AvlNode> node) {
    if (node == nullptr) {
        throw std::invalid_argument("rotate called on null tree!");
    }
    if (node->left == nullptr) {
        throw std::invalid_argument("rotate called on left null tree (no where to rotate)!");
    }
    // after rotation to the right, the new head is the node's left.
    auto newHead = node->left;

    // then the current head would have the left's right as it's left since all values are between left < ... < head
    node->left = newHead->right;
    // then the right of the new head is the current head.
    newHead->right = node;

    node->update();

    newHead->update();

    return newHead;
}

template<typename TKey, typename TValue>
shared_ptr<typename Avl<TKey, TValue>::AvlNode>
Avl<TKey, TValue>::updateTree(shared_ptr<AvlNode> node) {
    if (!node) return node;

    // left heavy
    if (node->balance > 1) {
        // LL if left child is left-heavy or balanced
        if (node->left->balance >= 0) {
            return rotateRight(node);
        } else {
            // LR
            node->left = rotateLeft(node->left);
            return rotateRight(node);
        }
    }

    // right heavy
    if (node->balance < -1) {
        // RR if right child is right-heavy or balanced
        if (node->right->balance <= 0) {
            return rotateLeft(node);
        } else {
            // RL
            node->right = rotateRight(node->right);
            return rotateLeft(node);
        }
    }

    // balanced
    return node;
}


template<typename TKey, typename TValue>
shared_ptr<typename Avl<TKey, TValue>::AvlNode> Avl<TKey, TValue>::binary_search(shared_ptr<AvlNode> head,
    const TKey &key) const {
    // nowhere to search
    if (head == nullptr) return nullptr;

    if (head->key == key) return head;
    if (key < head->key) return binary_search(head->left, key);

    return binary_search(head->right, key);
}

// this inserts and with recursion keeps track of the insertation path.
template<typename TKey, typename TValue>
shared_ptr<typename Avl<TKey, TValue>::AvlNode> Avl<TKey, TValue>::m_insert(shared_ptr<AvlNode> node, const TKey &key,
                                                                            const shared_ptr<TValue> &value) {
    if (node == nullptr) {
        // augment
        maxKey = std::max(maxKey, key);
        m_size++;
        // insert here. so create new node
        return make_shared<AvlNode>(key, value);
    }
    if (key < node->key) {
        node->left = m_insert(node->left, key, value);
    } else if (key > node->key) {
        node->right = m_insert(node->right, key, value);
    } else {
        throw std::invalid_argument("duplicate key");

        // key already exists... (check if throw error or we assume this situation does not occur (ask on piazza or something))
        return node;
    }

    // after we inserted, we need to update, then rotate accordingly.
    node->update();

    return updateTree(node);
}

template<typename TKey, typename TValue>
shared_ptr<typename Avl<TKey, TValue>::AvlNode> Avl<TKey, TValue>::m_remove(shared_ptr<AvlNode> node, const TKey &key) {
    if (node == nullptr) {
        // the key does not exist! (invalid op) or maybe just return the node?
        throw std::invalid_argument("remove() called on null tree!");
    }

    if (key < node->key) {
        node->left = m_remove(node->left, key);
    } else if (key > node->key) {
        node->right = m_remove(node->right, key);
    } else {
        // this is the node to delete.


        // at most one child
        if (node->left == nullptr || node->right == nullptr) {
            shared_ptr<AvlNode> child = (node->left != nullptr) ? node->left : node->right;

            if (child == nullptr) {
                // leaf
                return nullptr;
            } else {
                // has 1 child
                return child;
            }
        }

        // two children
        auto succ = node->right;
        while (succ->left != nullptr) {
            succ = succ->left;
        }

        // copy successor's key/value into this node
        node->key = succ->key;
        node->value = succ->value;

        // remove the successor node from the right subtree
        node->right = m_remove(node->right, succ->key);
    }

    // after delete, we need to update, then rotate accordingly.
    node->update();

    return updateTree(node);
}


template<typename TKey, typename TValue>
void Avl<TKey, TValue>::m_inorder(shared_ptr<AvlNode> node, inorder_callback_type cb) {
    if (node == nullptr) return;

    m_inorder(node->left, cb);
    cb(node->value);
    m_inorder(node->right, cb);
}


template<typename TKey, typename TValue>
shared_ptr<TValue> Avl<TKey, TValue>::find(const TKey &key) const {
    auto searchResult = binary_search(root, key);
    if (searchResult == nullptr) return nullptr;
    return searchResult->value;
}

template<typename TKey, typename TValue>
pair<TKey, std::shared_ptr<TValue> > Avl<TKey, TValue>::m_findMax(shared_ptr<AvlNode> node) const {
    if (!node) {
        throw std::logic_error("m_findMax on empty tree");
    }

    while (node->right) {
        node = node->right;
    }

    return {node->key, node->value};
}

template<typename TKey, typename TValue>
void Avl<TKey, TValue>::m_updateMax()  {
    if (root != nullptr) {
        auto maxElm = findMax();;
        maxKey = maxElm.first;
        maxValue = maxElm.second;
    } else {
        maxKey = TKey();
        maxValue = nullptr;
    }
}


template<typename TKey, typename TValue>
pair<TKey, std::shared_ptr<TValue> > Avl<TKey, TValue>::findMax() const {
    return m_findMax(root);
}


template<typename TKey, typename TValue>
shared_ptr<TValue> Avl<TKey, TValue>::insert(const TKey &key, const TValue &value) {
    auto value_ptr = make_shared<TValue>(value);

    root = m_insert(root, key, value_ptr);

    m_updateMax();

    return value_ptr;
}

template<typename TKey, typename TValue>
void Avl<TKey, TValue>::remove(const TKey &key) {
    root = m_remove(root, key);

    m_updateMax();

    // removed
    m_size--;
}

template<typename TKey, typename TValue>
void Avl<TKey, TValue>::inorder(inorder_callback_type cb) {
    return m_inorder(root, cb);
}

template<typename TKey, typename TValue>
bool Avl<TKey, TValue>::isEmpty() const {
    return root == nullptr;
}

template<typename TKey, typename TValue>
bool Avl<TKey, TValue>::exists(const TKey &key) const {
    return find(key) != nullptr;
}

template<typename TKey, typename TValue>
TKey Avl<TKey, TValue>::getMaxKey() const {
    return maxKey;
}

template<typename TKey, typename TValue>
int Avl<TKey, TValue>::size() const {
    return m_size;
}
