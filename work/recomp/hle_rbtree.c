/* HLE de l'arbre rouge-noir de libstdc++ (std::map / std::set) sur la memoire invitee.
 * _Rb_tree_node_base : { int couleur (0 rouge, 1 noir); parent; left; right } (16 octets). */
#include <stdint.h>
#include "rt.h"

enum { RED = 0, BLACK = 1 };
#define COL(n) M32((n) + 0)
#define PAR(n) M32((n) + 4)
#define LEF(n) M32((n) + 8)
#define RIG(n) M32((n) + 12)

static void rotate_left(uint32_t x, uint32_t header)
{
    uint32_t y = RIG(x);
    RIG(x) = LEF(y);
    if (LEF(y)) PAR(LEF(y)) = x;
    PAR(y) = PAR(x);
    if (x == PAR(header)) PAR(header) = y;
    else if (x == LEF(PAR(x))) LEF(PAR(x)) = y;
    else RIG(PAR(x)) = y;
    LEF(y) = x;
    PAR(x) = y;
}

static void rotate_right(uint32_t x, uint32_t header)
{
    uint32_t y = LEF(x);
    LEF(x) = RIG(y);
    if (RIG(y)) PAR(RIG(y)) = x;
    PAR(y) = PAR(x);
    if (x == PAR(header)) PAR(header) = y;
    else if (x == RIG(PAR(x))) RIG(PAR(x)) = y;
    else LEF(PAR(x)) = y;
    RIG(y) = x;
    PAR(x) = y;
}

static uint32_t minimum(uint32_t x) { while (LEF(x)) x = LEF(x); return x; }
static uint32_t maximum(uint32_t x) { while (RIG(x)) x = RIG(x); return x; }

EXT(__ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base)
{
    uint32_t x = A32(0);
    if (RIG(x)) {
        x = RIG(x);
        while (LEF(x)) x = LEF(x);
    } else {
        uint32_t y = PAR(x);
        while (x == RIG(y)) { x = y; y = PAR(y); }
        if (RIG(x) != y) x = y;
    }
    RET32(x);
}

EXT(__ZSt18_Rb_tree_decrementPSt18_Rb_tree_node_base)
{
    uint32_t x = A32(0);
    if (COL(x) == RED && PAR(PAR(x)) == x) {
        x = RIG(x);
    } else if (LEF(x)) {
        uint32_t y = LEF(x);
        while (RIG(y)) y = RIG(y);
        x = y;
    } else {
        uint32_t y = PAR(x);
        while (x == LEF(y)) { x = y; y = PAR(y); }
        x = y;
    }
    RET32(x);
}

/* _Rb_tree_insert_and_rebalance(bool insert_left, node *x, node *p, node &header) */
EXT(__ZSt29_Rb_tree_insert_and_rebalancebPSt18_Rb_tree_node_baseS0_RS_)
{
    uint32_t insert_left = A32(0) & 0xff, x = A32(1), p = A32(2), header = A32(3);
    PAR(x) = p;
    LEF(x) = 0;
    RIG(x) = 0;
    COL(x) = RED;
    if (insert_left) {
        LEF(p) = x;
        if (p == header) { PAR(header) = x; RIG(header) = x; }
        else if (p == LEF(header)) LEF(header) = x;
    } else {
        RIG(p) = x;
        if (p == RIG(header)) RIG(header) = x;
    }
    while (x != PAR(header) && COL(PAR(x)) == RED) {
        uint32_t xpp = PAR(PAR(x));
        if (PAR(x) == LEF(xpp)) {
            uint32_t y = RIG(xpp);
            if (y && COL(y) == RED) {
                COL(PAR(x)) = BLACK; COL(y) = BLACK; COL(xpp) = RED; x = xpp;
            } else {
                if (x == RIG(PAR(x))) { x = PAR(x); rotate_left(x, header); }
                COL(PAR(x)) = BLACK; COL(PAR(PAR(x))) = RED;
                rotate_right(PAR(PAR(x)), header);
            }
        } else {
            uint32_t y = LEF(xpp);
            if (y && COL(y) == RED) {
                COL(PAR(x)) = BLACK; COL(y) = BLACK; COL(xpp) = RED; x = xpp;
            } else {
                if (x == LEF(PAR(x))) { x = PAR(x); rotate_right(x, header); }
                COL(PAR(x)) = BLACK; COL(PAR(PAR(x))) = RED;
                rotate_left(PAR(PAR(x)), header);
            }
        }
    }
    COL(PAR(header)) = BLACK;
}

/* _Rb_tree_rebalance_for_erase(node *z, node &header) -> noeud a liberer */
EXT(__ZSt28_Rb_tree_rebalance_for_erasePSt18_Rb_tree_node_baseRS_)
{
    uint32_t z = A32(0), header = A32(1);
    uint32_t y = z, x = 0, x_parent = 0;
    if (LEF(y) == 0) x = RIG(y);
    else if (RIG(y) == 0) x = LEF(y);
    else { y = minimum(RIG(y)); x = RIG(y); }

    if (y != z) {
        PAR(LEF(z)) = y;
        LEF(y) = LEF(z);
        if (y != RIG(z)) {
            x_parent = PAR(y);
            if (x) PAR(x) = PAR(y);
            LEF(PAR(y)) = x;
            RIG(y) = RIG(z);
            PAR(RIG(z)) = y;
        } else {
            x_parent = y;
        }
        if (PAR(header) == z) PAR(header) = y;
        else if (LEF(PAR(z)) == z) LEF(PAR(z)) = y;
        else RIG(PAR(z)) = y;
        PAR(y) = PAR(z);
        uint32_t t = COL(y); COL(y) = COL(z); COL(z) = t;
        y = z;
    } else {
        x_parent = PAR(y);
        if (x) PAR(x) = PAR(y);
        if (PAR(header) == z) PAR(header) = x;
        else if (LEF(PAR(z)) == z) LEF(PAR(z)) = x;
        else RIG(PAR(z)) = x;
        if (LEF(header) == z) LEF(header) = RIG(z) == 0 ? PAR(z) : minimum(x);
        if (RIG(header) == z) RIG(header) = LEF(z) == 0 ? PAR(z) : maximum(x);
    }
    if (COL(y) != RED) {
        while (x != PAR(header) && (x == 0 || COL(x) == BLACK)) {
            if (x == LEF(x_parent)) {
                uint32_t w = RIG(x_parent);
                if (COL(w) == RED) {
                    COL(w) = BLACK; COL(x_parent) = RED;
                    rotate_left(x_parent, header);
                    w = RIG(x_parent);
                }
                if ((LEF(w) == 0 || COL(LEF(w)) == BLACK) && (RIG(w) == 0 || COL(RIG(w)) == BLACK)) {
                    COL(w) = RED; x = x_parent; x_parent = PAR(x_parent);
                } else {
                    if (RIG(w) == 0 || COL(RIG(w)) == BLACK) {
                        COL(LEF(w)) = BLACK; COL(w) = RED;
                        rotate_right(w, header);
                        w = RIG(x_parent);
                    }
                    COL(w) = COL(x_parent); COL(x_parent) = BLACK;
                    if (RIG(w)) COL(RIG(w)) = BLACK;
                    rotate_left(x_parent, header);
                    break;
                }
            } else {
                uint32_t w = LEF(x_parent);
                if (COL(w) == RED) {
                    COL(w) = BLACK; COL(x_parent) = RED;
                    rotate_right(x_parent, header);
                    w = LEF(x_parent);
                }
                if ((RIG(w) == 0 || COL(RIG(w)) == BLACK) && (LEF(w) == 0 || COL(LEF(w)) == BLACK)) {
                    COL(w) = RED; x = x_parent; x_parent = PAR(x_parent);
                } else {
                    if (LEF(w) == 0 || COL(LEF(w)) == BLACK) {
                        COL(RIG(w)) = BLACK; COL(w) = RED;
                        rotate_left(w, header);
                        w = LEF(x_parent);
                    }
                    COL(w) = COL(x_parent); COL(x_parent) = BLACK;
                    if (LEF(w)) COL(LEF(w)) = BLACK;
                    rotate_right(x_parent, header);
                    break;
                }
            }
        }
        if (x) COL(x) = BLACK;
    }
    RET32(y);
}
