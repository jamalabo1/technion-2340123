//
// Created by jamal on 02/12/2025.
//

#ifndef AVL_H
#define AVL_H

#include <memory>
#include <functional>

// TComparable must have compare operations.
template<typename TKey, typename TValue>
class Avl {
private:
    using inorder_callback_type = const std::function<void(std::shared_ptr<TValue>)> &;

    class AvlNode {
    public:
        TKey key;
        std::shared_ptr<TValue> value;

        std::shared_ptr<AvlNode> right;
        std::shared_ptr<AvlNode> left;

        int height;
        short balance;

        AvlNode(const TKey &key, std::shared_ptr<TValue> value) : key(key), value(value), right(nullptr), left(nullptr),
                                                                  height(0), balance(0) {
        }

        void update();

        static int heightOf(const std::shared_ptr<AvlNode> &node);
    };

    int m_size = 0;

    std::shared_ptr<AvlNode> root;

    std::shared_ptr<AvlNode> rotateLeft(std::shared_ptr<AvlNode> node);

    std::shared_ptr<AvlNode> rotateRight(std::shared_ptr<AvlNode> node);

    // std::shared_ptr<AvlNode> updateTree(
    //     std::shared_ptr<AvlNode> node,
    //     const TKey &key,
    //     bool reverseMode = false
    // );
    std::shared_ptr<AvlNode> updateTree(
        std::shared_ptr<AvlNode> node
    );

    // this returns to nodes, the one is the searched node, and the second is the last node to be searched.
    std::shared_ptr<AvlNode> binary_search(std::shared_ptr<AvlNode> head, const TKey &key) const;

    std::shared_ptr<AvlNode> m_insert(std::shared_ptr<AvlNode> node, const TKey &key, const std::shared_ptr<TValue> &);

    std::shared_ptr<AvlNode> m_remove(std::shared_ptr<AvlNode> node, const TKey &key);

    // in-order
    void m_inorder(std::shared_ptr<AvlNode> node, inorder_callback_type cb);

    // augmented max id
    TKey maxKey;
    std::shared_ptr<TValue> maxValue;

    std::pair<TKey, std::shared_ptr<TValue> > m_findMax(std::shared_ptr<AvlNode> node) const;

    void m_updateMax() ;

public:
    std::shared_ptr<TValue> find(const TKey &key) const;

    std::pair<TKey, std::shared_ptr<TValue> > findMax() const;


    /***
     * @details inserts data to avl
     */
    std::shared_ptr<TValue> insert(const TKey &, const TValue &);

    void remove(const TKey &);

    // in order
    void inorder(inorder_callback_type cb);

    bool isEmpty() const;

    bool exists(const TKey &) const;

    int size() const;

    TKey getMaxKey() const;
};


#endif //AVL_H
