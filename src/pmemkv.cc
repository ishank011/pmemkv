/*
 * Copyright 2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <algorithm>
#include <cstring>
#include <iostream>
#include <list>
#include <unistd.h>
#include "pmemkv.h"

#define DO_LOG 0
#define LOG(msg) if (DO_LOG) std::cout << "[pmemkv] " << msg << "\n"

namespace pmemkv {

KVTree::KVTree(const string& path, const size_t size) : pmpath(path) {
    if (access(path.c_str(), F_OK) != 0) {
        LOG("Creating pool, path=" << path << ", size=" << std::to_string(size));
        pmpool = pool<KVRoot>::create(path.c_str(), LAYOUT, size, S_IRWXU);
        pmsize = size;
    } else {
        LOG("Opening existing pool, path=" << path);
        pmpool = pool<KVRoot>::open(path.c_str(), LAYOUT);
        struct stat st;
        stat(path.c_str(), &st);
        pmsize = (size_t) st.st_size;
    }
    Recover();
    LOG("Opened ok");
}

KVTree::~KVTree() {
    LOG("Closing");
    Shutdown();
    pmpool.close();
    LOG("Closed ok");
}

// ===============================================================================================
// KEY/VALUE METHODS
// ===============================================================================================

void KVTree::Analyze(KVTreeAnalysis& analysis) {
    LOG("Analyzing");
    analysis.leaf_empty = 0;
    analysis.leaf_prealloc = leaves_prealloc.size();
    analysis.leaf_total = 0;
    analysis.path = pmpath;
    analysis.size = pmsize;

    // iterate persistent leaves for stats
    auto leaf = pmpool.get_root()->head;
    while (leaf) {
        bool empty = true;
        for (int slot = LEAF_KEYS; slot--;) {
            if (leaf->slots[slot].get_ro().hash() != 0) {
                empty = false;
                break;
            }
        }
        if (empty) analysis.leaf_empty++;
        analysis.leaf_total++;
        leaf = leaf->next;  // advance to next linked leaf
    }
    LOG("Analyzed ok");
}

KVStatus KVTree::Get(const string& key, const size_t limit, char* value, uint32_t* valuebytes) {
    LOG("Get for key=" << key.c_str());
    auto leafnode = LeafSearch(key);
    if (!leafnode) {
        LOG("   head not present");
        return NOT_FOUND;
    }
    const uint8_t hash = PearsonHash(key.c_str(), key.size());
    for (int slot = LEAF_KEYS; slot--;) {
        if (leafnode->hashes[slot] == hash) {
            if (strcmp(leafnode->keys[slot].c_str(), key.c_str()) == 0) {
                auto kv = leafnode->leaf->slots[slot].get_ro();
                auto vs = kv.valsize();
                if (vs < limit - 1) {
                    LOG("   found value, slot=" << slot << ", size=" << std::to_string(vs));
                    memcpy(value, kv.val(), vs);
                    *valuebytes = vs;
                    return OK;
                } else {
                    return FAILED;
                }
            }
        }
    }
    LOG("   could not find key");
    return NOT_FOUND;
}

KVStatus KVTree::Get(const string& key, string* value) {
    LOG("Get for key=" << key.c_str());
    auto leafnode = LeafSearch(key);
    if (!leafnode) {
        LOG("   head not present");
        return NOT_FOUND;
    }
    const uint8_t hash = PearsonHash(key.c_str(), key.size());
    for (int slot = LEAF_KEYS; slot--;) {
        if (leafnode->hashes[slot] == hash) {
            if (strcmp(leafnode->keys[slot].c_str(), key.c_str()) == 0) {
                auto kv = leafnode->leaf->slots[slot].get_ro();
                LOG("   found value, slot=" << slot << ", size=" << std::to_string(kv.valsize()));
                value->append(kv.val());
                return OK;
            }
        }
    }
    LOG("   could not find key");
    return NOT_FOUND;
}

KVStatus KVTree::Put(const string& key, const string& value) {
    LOG("Put key=" << key.c_str() << ", value.size=" << std::to_string(value.size()));
    try {
        const uint8_t hash = PearsonHash(key.c_str(), key.size());
        auto leafnode = LeafSearch(key);
        if (!leafnode) {
            LOG("   adding head leaf");
            leafnode = new KVLeafNode();
            leafnode->is_leaf = true;
            transaction::exec_tx(pmpool, [&] {
                if (!leaves_prealloc.empty()) {
                    leafnode->leaf = leaves_prealloc.back();
                    leaves_prealloc.pop_back();
                } else {
                    auto root = pmpool.get_root();
                    auto old_head = root->head;
                    auto new_leaf = make_persistent<KVLeaf>();
                    root->head = new_leaf;
                    new_leaf->next = old_head;
                    leafnode->leaf = new_leaf;
                }
                LeafFillSpecificSlot(leafnode, hash, key, value, 0);
            });
            tree_top = leafnode;
        } else if (LeafFillSlotForKey(leafnode, hash, key, value)) {
            // nothing else to do
        } else {
            LeafSplitFull(leafnode, hash, key, value);
        }
        return OK;
    } catch (nvml::transaction_alloc_error) {
        return FAILED;
    } catch (nvml::transaction_error) {
        return FAILED;
    }
}

KVStatus KVTree::Remove(const string& key) {
    LOG("Remove key=" << key.c_str());
    auto leafnode = LeafSearch(key);
    if (!leafnode) {
        LOG("   head not present");
        return OK;
    }
    const uint8_t hash = PearsonHash(key.c_str(), key.size());
    for (int slot = LEAF_KEYS; slot--;) {
        if (leafnode->hashes[slot] == hash) {
            if (strcmp(leafnode->keys[slot].c_str(), key.c_str()) == 0) {
                LOG("   freeing slot=" << slot);
                leafnode->hashes[slot] = 0;
                leafnode->keys[slot].clear();
                auto leaf = leafnode->leaf;
                transaction::exec_tx(pmpool, [&] {
                    leaf->slots[slot].get_rw().clear();
                });
                break;  // no duplicate keys allowed
            }
        }
    }
    return OK;
}

// ===============================================================================================
// PROTECTED LEAF METHODS
// ===============================================================================================

KVLeafNode* KVTree::LeafSearch(const string& key) {
    KVNode* node = tree_top;
    if (node == nullptr) return nullptr;
    bool matched;
    while (!node->is_leaf) {
        matched = false;
        KVInnerNode* inner = (KVInnerNode*) node;
        const uint8_t keycount = inner->keycount;
        for (uint8_t idx = 0; idx < keycount; idx++) {
            node = inner->children[idx];
            if (strcmp(key.c_str(), inner->keys[idx].c_str()) <= 0) {
                matched = true;
                break;
            }
        }
        if (!matched) node = inner->children[keycount];
    }
    return (KVLeafNode*) node;
}

void KVTree::LeafFillEmptySlot(KVLeafNode* leafnode, const uint8_t hash,
                               const string& key, const string& value) {
    for (int slot = LEAF_KEYS; slot--;) {
        if (leafnode->hashes[slot] == 0) {
            LeafFillSpecificSlot(leafnode, hash, key, value, slot);
            return;
        }
    }
}

bool KVTree::LeafFillSlotForKey(KVLeafNode* leafnode, const uint8_t hash,
                                const string& key, const string& value) {
    // scan for empty/matching slots
    int last_empty_slot = -1;
    int key_match_slot = -1;
    for (int slot = LEAF_KEYS; slot--;) {
        auto slot_hash = leafnode->hashes[slot];
        if (slot_hash == 0) {
            last_empty_slot = slot;
        } else if (slot_hash == hash) {
            if (strcmp(leafnode->keys[slot].c_str(), key.c_str()) == 0) {
                key_match_slot = slot;
                break;  // no duplicate keys allowed
            }
        }
    }

    // update suitable slot if found
    int slot = key_match_slot >= 0 ? key_match_slot : last_empty_slot;
    if (slot >= 0) {
        LOG("   filling slot=" << slot);
        transaction::exec_tx(pmpool, [&] {
            LeafFillSpecificSlot(leafnode, hash, key, value, slot);
        });
    }
    return slot >= 0;
}

void KVTree::LeafFillSpecificSlot(KVLeafNode* leafnode, const uint8_t hash,
                                  const string& key, const string& value, const int slot) {
    if (leafnode->hashes[slot] == 0) {
        leafnode->hashes[slot] = hash;
        leafnode->keys[slot] = key;
    }
    leafnode->leaf->slots[slot].get_rw().set(hash, key, value);
}

void KVTree::LeafSplitFull(KVLeafNode* leafnode, const uint8_t hash,
                           const string& key, const string& value) {
    string keys[LEAF_KEYS + 1];
    keys[LEAF_KEYS] = key;
    for (int slot = LEAF_KEYS; slot--;) keys[slot] = leafnode->keys[slot];
    std::sort(std::begin(keys), std::end(keys), [](const string& lhs, const string& rhs) {
        return (strcmp(lhs.c_str(), rhs.c_str()) < 0);
    });
    string split_key = keys[LEAF_KEYS_MIDPOINT];
    LOG("   splitting leaf at key=" << split_key);

    // split leaf into two leaves, moving slots that sort above split key to new leaf
    auto new_leafnode = new KVLeafNode();
    try {
        new_leafnode->parent = leafnode->parent;
        new_leafnode->is_leaf = true;
        transaction::exec_tx(pmpool, [&] {
            persistent_ptr<KVLeaf> new_leaf;
            if (!leaves_prealloc.empty()) {
                new_leaf = leaves_prealloc.back();
                new_leafnode->leaf = new_leaf;
                leaves_prealloc.pop_back();
            } else {
                auto root = pmpool.get_root();
                auto old_head = root->head;
                new_leaf = make_persistent<KVLeaf>();
                root->head = new_leaf;
                new_leaf->next = old_head;
                new_leafnode->leaf = new_leaf;
            }
            for (int slot = LEAF_KEYS; slot--;) {
                if (strcmp(leafnode->keys[slot].c_str(), split_key.data()) > 0) {
                    new_leaf->slots[slot].swap(leafnode->leaf->slots[slot]);
                    new_leafnode->hashes[slot] = leafnode->hashes[slot];
                    new_leafnode->keys[slot] = leafnode->keys[slot];
                    leafnode->hashes[slot] = 0;
                    leafnode->keys[slot].clear();
                }
            }
            auto target = strcmp(key.c_str(), split_key.data()) > 0 ? new_leafnode : leafnode;
            LeafFillEmptySlot(target, hash, key, value);
        });
    } catch (...) {
        delete new_leafnode;
        throw;
    }

    // recursively update volatile parents outside persistent transaction
    InnerUpdateAfterSplit(leafnode, new_leafnode, &split_key);
}

void KVTree::InnerUpdateAfterSplit(KVNode* node, KVNode* new_node, string* split_key) {
    if (!node->parent) {
        LOG("   creating new top node for split_key=" << *split_key);
        auto top = new KVInnerNode();
        top->keycount = 1;
        top->keys[0] = *split_key;
        top->children[0] = node;
        top->children[1] = new_node;
        node->parent = top;
        new_node->parent = top;
        tree_top = top;                                                  // assign new top node
        return;                                                          // end recursion
    }

    LOG("   updating parents for split_key=" << *split_key);
    KVInnerNode* inner = (KVInnerNode*) node->parent;
    { // insert split_key and new_node into inner node in sorted order
        const uint8_t keycount = inner->keycount;
        int idx = 0;  // position where split_key should be inserted
        while (idx < keycount && inner->keys[idx].compare(*split_key) <= 0) idx++;
        for (int i = keycount - 1; i >= idx; i--) inner->keys[i + 1] = inner->keys[i];
        for (int i = keycount; i >= idx; i--) inner->children[i + 1] = inner->children[i];
        inner->keys[idx] = *split_key;
        inner->children[idx + 1] = new_node;
        inner->keycount = (uint8_t) (keycount + 1);
    }
    const uint8_t keycount = inner->keycount;
    if (keycount <= INNER_KEYS) return;                                  // end recursion

    // split inner node at the midpoint, update parents as needed
    auto new_inner = new KVInnerNode();                                  // allocate new node
    new_inner->parent = inner->parent;                                   // set parent reference
    for (int i = INNER_KEYS_UPPER; i < keycount; i++) {                  // copy all upper keys
        new_inner->keys[i - INNER_KEYS_UPPER] = inner->keys[i];          // copy key string
    }
    for (int i = INNER_KEYS_UPPER; i < keycount + 1; i++) {              // copy all upper children
        new_inner->children[i - INNER_KEYS_UPPER] = inner->children[i];  // copy child reference
        new_inner->children[i - INNER_KEYS_UPPER]->parent = new_inner;   // set parent reference
    }
    new_inner->keycount = INNER_KEYS_MIDPOINT;                           // always half the keys
    string new_split_key = inner->keys[INNER_KEYS_MIDPOINT];             // save for recursion
    inner->keycount = INNER_KEYS_MIDPOINT;                               // half of keys remain
    InnerUpdateAfterSplit(inner, new_inner, &new_split_key);             // recursive update
}

// ===============================================================================================
// PROTECTED LIFECYCLE METHODS
// ===============================================================================================

void KVTree::Recover() {
    LOG("Recovering");

    // traverse persistent leaves to build list of leaves to recover
    std::list<KVRecoveredLeaf*> leaves;
    auto leaf = pmpool.get_root()->head;
    while (leaf) {
        auto leafnode = new KVLeafNode();
        leafnode->leaf = leaf;
        leafnode->is_leaf = true;

        // find highest sorting key in leaf, while recovering all hashes
        char* max_key = nullptr;
        for (int slot = LEAF_KEYS; slot--;) {
            auto kvslot = leaf->slots[slot].get_ro();
            leafnode->hashes[slot] = kvslot.hash();
            if (leafnode->hashes[slot] == 0) continue;
            const char* key = kvslot.key();
            if (max_key == nullptr || strcmp(max_key, key) < 0) max_key = (char*) key;
            leafnode->keys[slot] = key;
        }

        // use highest sorting key to decide how to recover the leaf
        if (max_key == nullptr) {
            leaves_prealloc.push_back(leaf);
        } else {
            auto rleaf = new KVRecoveredLeaf;
            rleaf->leafnode = leafnode;
            rleaf->max_key = max_key;
            leaves.push_back(rleaf);
        }

        leaf = leaf->next;  // advance to next linked leaf
    }

    // sort recovered leaves in ascending key order
    leaves.sort([](const KVRecoveredLeaf* lhs, const KVRecoveredLeaf* rhs) {
        return (strcmp(lhs->max_key, rhs->max_key) < 0);
    });

    // reconstruct top/inner nodes using adjacent pairs of recovered leaves
    tree_top = nullptr;
    while (!leaves.empty()) {
        KVRecoveredLeaf* rleaf = leaves.front();
        KVLeafNode* leafnode = rleaf->leafnode;
        if (tree_top == nullptr) tree_top = leafnode;
        leaves.pop_front();
        if (!leaves.empty()) {
            string split_key = string(rleaf->max_key);
            KVLeafNode* nextnode = leaves.front()->leafnode;
            nextnode->parent = leafnode->parent;
            InnerUpdateAfterSplit(leafnode, nextnode, &split_key);
        }
        delete rleaf;
    }

    LOG("Recovered ok");
}

void KVTree::Shutdown() {
    LOG("Shutting down");
    if (tree_top) Shutdown(tree_top);
    LOG("Shut down ok");
}

void KVTree::Shutdown(KVNode* node) {
    if (node->is_leaf) {
        auto leafnode = (KVLeafNode*) node;
        delete leafnode;
    } else {
        auto inner = (KVInnerNode*) node;
        for (uint8_t idx = 0; idx < inner->keycount + 1; idx++) {
            Shutdown(inner->children[idx]);
        }
        delete inner;
    }
}

// ===============================================================================================
// PEARSON HASH METHODS
// ===============================================================================================

// Pearson hashing lookup table from RFC 3074
const uint8_t PEARSON_LOOKUP_TABLE[256] = {
        251, 175, 119, 215, 81, 14, 79, 191, 103, 49, 181, 143, 186, 157, 0,
        232, 31, 32, 55, 60, 152, 58, 17, 237, 174, 70, 160, 144, 220, 90, 57,
        223, 59, 3, 18, 140, 111, 166, 203, 196, 134, 243, 124, 95, 222, 179,
        197, 65, 180, 48, 36, 15, 107, 46, 233, 130, 165, 30, 123, 161, 209, 23,
        97, 16, 40, 91, 219, 61, 100, 10, 210, 109, 250, 127, 22, 138, 29, 108,
        244, 67, 207, 9, 178, 204, 74, 98, 126, 249, 167, 116, 34, 77, 193,
        200, 121, 5, 20, 113, 71, 35, 128, 13, 182, 94, 25, 226, 227, 199, 75,
        27, 41, 245, 230, 224, 43, 225, 177, 26, 155, 150, 212, 142, 218, 115,
        241, 73, 88, 105, 39, 114, 62, 255, 192, 201, 145, 214, 168, 158, 221,
        148, 154, 122, 12, 84, 82, 163, 44, 139, 228, 236, 205, 242, 217, 11,
        187, 146, 159, 64, 86, 239, 195, 42, 106, 198, 118, 112, 184, 172, 87,
        2, 173, 117, 176, 229, 247, 253, 137, 185, 99, 164, 102, 147, 45, 66,
        231, 52, 141, 211, 194, 206, 246, 238, 56, 110, 78, 248, 63, 240, 189,
        93, 92, 51, 53, 183, 19, 171, 72, 50, 33, 104, 101, 69, 8, 252, 83, 120,
        76, 135, 85, 54, 202, 125, 188, 213, 96, 235, 136, 208, 162, 129, 190,
        132, 156, 38, 47, 1, 7, 254, 24, 4, 216, 131, 89, 21, 28, 133, 37, 153,
        149, 80, 170, 68, 6, 169, 234, 151
};

// Modified Pearson hashing algorithm from RFC 3074
uint8_t KVTree::PearsonHash(const char* data, const size_t size) {
    uint8_t hash = (uint8_t) size;
    for (size_t i = size; i > 0;) {
        hash = PEARSON_LOOKUP_TABLE[hash ^ data[--i]];
    }
    // MODIFICATION START
    return (hash == 0) ? (uint8_t) 1 : hash;                             // 0 reserved for "null"
    // MODIFICATION END
}

// ===============================================================================================
// SLOT CLASS METHODS
// ===============================================================================================

void KVSlot::clear() {
    if (kv) delete_persistent<char[]>(kv, ks + vs + 2);                  // free buffer if present
    ph = 0;                                                              // clear pearson hash
    ks = 0;                                                              // clear key size
    vs = 0;                                                              // clear value size
    kv = nullptr;                                                        // clear buffer pointer
}

void KVSlot::set(const uint8_t hash, const string& key, const string& value) {
    if (kv) delete_persistent<char[]>(kv, ks + vs + 2);                  // free buffer if present
    ph = hash;                                                           // copy hash into leaf
    ks = (uint32_t) key.size();                                          // read key size
    vs = (uint32_t) value.size();                                        // read value size
    size_t size = ks + vs + 2;                                           // set total size
    kv = make_persistent<char[]>(size);                                  // reserve buffer space
    char* kvptr = kv.get();                                              // set ptr into buffer
    memcpy(kvptr, key.data(), ks);                                       // copy key into buffer
    kvptr += ks + 1;                                                     // advance ptr past key
    memcpy(kvptr, value.data(), vs);                                     // copy value into buffer
}

// ===============================================================================================
// C API FOR LANGUAGE BINDINGS
// ===============================================================================================

extern "C" KVTree* kvtree_open(const char* path,
                               const size_t size) {
    try {
        return new KVTree(path, size);
    } catch (...) {
        return nullptr;
    }
};

extern "C" void kvtree_close(KVTree* kv) {
    delete kv;
};

extern "C" int8_t kvtree_get(KVTree* kv,
                             const char* key,
                             const size_t limit,
                             char* value,
                             uint32_t* valuebytes) {
    return kv->Get(key, limit, value, valuebytes);
}

extern "C" int8_t kvtree_put(KVTree* kv,
                             const char* key,
                             const char* value,
                             const uint32_t* valuebytes) {
    return kv->Put(key, string(value, *valuebytes));  // todo need put with pointer/length (#39)
}

extern "C" int8_t kvtree_remove(KVTree* kv,
                                const char* key) {
    return kv->Remove(key);
};

extern "C" size_t kvtree_size(KVTree* kv) {
    KVTreeAnalysis analysis = {};
    kv->Analyze(analysis);
    return analysis.size;
}

// todo missing test cases for C API (#39)

} // namespace pmemkv
