#pragma once
#include <Object.hpp>

struct Block : public Object {
    Block(Vec2D size, std::unordered_map<int, std::string>&& fields);
    void collide(Player&) const override;
    bool touching(Player const& p) const override;

    /// PlayerObject::collidedWithObjectInternal. bare: a slope's side, which GD passes as a rect without an
    /// object (no X-snap off it, and every nearby slope can cover it)
    void collideBlock(Player&, bool bare) const;

    /// A cube landing on the block: PlayerObject::checkSnapJumpToObject
    void snapX(Player&) const;

    /// Whether a player rising into the block's underside is stopped by it (otherwise the block
    /// counts as touched lethally, which breaks a breakable block)
    virtual bool stopsHead(Player const&) const { return true; }
};

struct BreakableBlock : public Block {
    using Block::Block;

    void collide(Player&) const override;
    bool touching(Player const& p) const override;
    // A ufo breaks it with its head (GD: collidedWithObjectInternal, outside platformer)
    bool stopsHead(Player const& p) const override;
};
