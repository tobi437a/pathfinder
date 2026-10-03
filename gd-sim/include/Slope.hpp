#pragma once
#include <Block.hpp>

/// A CCRect in GD's coordinates (sim y + 90), with cocos2d's float arithmetic
struct GDRect {
    float x, y, w, h;

    float minX() const { return x; }
    float maxX() const { return x + w; }
    float minY() const { return y; }
    float maxY() const { return y + h; }
    /// cocos2d::CCRect::intersectsRect: touching edges count
    bool intersects(GDRect const& o) const {
        return !(maxX() < o.minX() || o.maxX() < minX() || maxY() < o.minY() || o.maxY() < minY());
    }
    /// GameObject::getObjectRect(w, h): the hitbox scaled by (w, h) around its centre
    static GDRect of(Entity const& e, float w = 1, float h = 1);
};

/**
 * Slopes (GD object type 25), ported from PlayerObject::collidedWithSlopeInternal and
 * preSlopeCollision. GD collides them in object order with pads, orbs and portals, before the blocks.
 */
struct Slope : public Block {
    /// GameObject::determineSlopeDirection: 0 floor slope rising to the right, 1 ceiling slope (flip y),
    /// 2 floor slope falling to the right (flip x), 3 ceiling slope (rotated 180), 4-7 the 90 degree turns
    int direction = 0;
    /// m_slopeUphill: the surface rises to the right
    bool uphill = false;
    /// m_slopeIsHazard
    bool hazard = false;

    Slope(Vec2D size, std::unordered_map<int, std::string>&& fields);
    /// GD's broadphase for slopes: the hitbox at twice its size
    bool touching(Player const&) const override;
    void collide(Player&) const override;

    /// The solid part is above the surface (directions 1, 3, 5, 6)
    bool ceiling() const;
    /// The vertical side is on the left (directions 2, 3, 4, 6)
    bool wallLeft() const;
    /// GameObject::slopeYPos: the surface's y at x in GD coordinates, continued past the slope's ends
    double surfaceY(float x) const;

private:
    /// PlayerObject::preSlopeCollision: a player beside the vertical side or beyond the flat side hits
    /// that side like a block. True if the slope collision is skipped.
    bool preCollide(Player& p, GDRect const& slope, GDRect const& player) const;
};

/// Hazard slopes are slopes that kill (m_slopeIsHazard)
struct SlopeHazard : public Slope {
    SlopeHazard(Vec2D size, std::unordered_map<int, std::string>&& fields) : Slope(size, std::move(fields)) {
        hazard = true;
    }
};
