#include <util.hpp>
#include <cmath>
#include <complex>

float slerp(float fromAngle, float toAngle, float t) {
    std::complex<float> fromVec = std::polar(1.0f, fromAngle * 0.5f);
    std::complex<float> toVec = std::polar(1.0f, toAngle * 0.5f);


    float dot = std::imag(fromVec) * std::imag(toVec) + std::real(fromVec) * std::real(toVec);
    if (dot < 0.0f) {
        dot *= -1;
        toVec *=  -1;
    }

    std::complex<float> weight = std::complex(1.0f - t, t);
    if (dot < 0.9999) {
        float between = std::acos(dot);

        weight *= between;
        weight = std::complex(std::sin(weight.real()), std::sin(weight.imag()));
        weight /= std::sin(between);
    }

    std::complex<float> interpVec = (weight.imag() * toVec) + (weight.real() * fromVec);
    return std::atan2(std::imag(interpVec), std::real(interpVec)) * 2;
}


Vec2D Vec2D::rotate(float angle, Vec2D const& pivot) const {
    if (angle == 0) return *this;

    Vec2D tmp = *this - pivot;

    float rad = deg2rad(angle);
    float s = std::sin(rad);
    float c = std::cos(rad);

    tmp = {tmp.x * c - tmp.y * s, tmp.x * s + tmp.y * c};
    tmp += pivot;

    return tmp;
}

// Very complicated (but very fast) way to check intersection
bool intersectOneWay(Entity const& a, Entity const& b) {
    float big = std::max(a.size.x, a.size.y) + std::max(b.size.x, b.size.y);
    if (std::abs(a.pos.x - b.pos.x) > big || std::abs(a.pos.y - b.pos.y) > big) {
        return false;
    }

    Entity tmp = b;

    tmp.rotation -= a.rotation;
    tmp.pos = tmp.pos.rotate(-a.rotation, a.pos);

    Vec2D corners[4] = {
        Vec2D(tmp.getLeft(), tmp.getBottom()).rotate(tmp.rotation, tmp.pos),
        Vec2D(tmp.getRight(), tmp.getBottom()).rotate(tmp.rotation, tmp.pos),
        Vec2D(tmp.getRight(), tmp.getTop()).rotate(tmp.rotation, tmp.pos),
        Vec2D(tmp.getLeft(), tmp.getTop()).rotate(tmp.rotation, tmp.pos)
    };

    float lastDiffX = 0;
    bool overlapX = false;

    float lastDiffY = 0;
    bool overlapY = false;

    for (auto vert : corners) {
        if (!overlapX) {
            float diffX = vert.x - a.pos.x;
            if ((vert.x >= a.getLeft() && vert.x <= a.getRight()) || (lastDiffX != 0 && std::signbit(lastDiffX) != std::signbit(diffX))) {
                overlapX = true;
            }
            lastDiffX = diffX;
        }
        if (!overlapY) {
            float diffY = vert.y - a.pos.y;
            if ((vert.y >= a.getBottom() && vert.y <= a.getTop()) || (lastDiffY != 0 && std::signbit(lastDiffY) != std::signbit(diffY))) {
                overlapY = true;
            }
            lastDiffY = diffY;
        }
    }

    return overlapX && overlapY;
}

bool Entity::intersects(Entity const& b) const {
    return intersectOneWay(*this, b) && intersectOneWay(b, *this);
}

namespace {

/// OBB2D::calculateWithCenter: corners centre -+ X -+ Y with X = (cos, sin) * w / 2, Y = (-sin, cos) * h / 2,
/// axes (corner 1 - corner 0, corner 3 - corner 0) divided by their squared length, and the projection of
/// corner 0 on each (a float widened to double)
struct GdObb {
    float c[4][2];
    float axis[2][2];
    double origin[2];

    explicit GdObb(Entity const& e) {
        float cx = e.pos.x, cy = e.pos.y + 90.f;
        float a = e.rotation * 0.017453292f;
        float sn = sinf(a), cs = cosf(a);
        float hw = e.size.x * 0.5f, hh = e.size.y * 0.5f;
        float xx = cs * hw, xy = sn * hw, yx = -sn * hh, yy = cs * hh;
        float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
        for (int i = 0; i < 4; ++i) {
            float px = sx[i] < 0 ? cx - xx : cx + xx, py = sx[i] < 0 ? cy - xy : cy + xy;
            c[i][0] = sy[i] < 0 ? px - yx : px + yx;
            c[i][1] = sy[i] < 0 ? py - yy : py + yy;
        }
        for (int k = 0; k < 2; ++k) {
            int j = k == 0 ? 1 : 3;
            float ax = c[j][0] - c[0][0], ay = c[j][1] - c[0][1];
            float inv = 1.0f / (ax * ax + ay * ay);
            axis[k][0] = ax * inv;
            axis[k][1] = ay * inv;
            float o = c[0][1] * axis[k][1] + c[0][0] * axis[k][0];
            origin[k] = o;
        }
    }

    /// OBB2D::overlaps1Way: the other box's corners projected on each axis reach [origin, origin + 1]
    bool overlaps1Way(GdObb const& o) const {
        for (int k = 0; k < 2; ++k) {
            double lo = 0, hi = 0;
            for (int i = 0; i < 4; ++i) {
                float t = axis[k][0] * o.c[i][0] + axis[k][1] * o.c[i][1];
                double d = t;
                if (i == 0 || d < lo) lo = d;
                if (i == 0 || hi < d) hi = d;
            }
            if (origin[k] + 1.0 < lo || hi < origin[k])
                return false;
        }
        return true;
    }
};

}

bool Entity::orientedOverlaps(Entity const& b) const {
    GdObb A(*this), B(b);
    return A.overlaps1Way(B) && B.overlaps1Way(A);
}


/*using Vec2 = Vec2D;
// Return normalized perpendicular axis from two points
static Vec2 edgeToAxis(const Vec2D& p1, const Vec2& p2) {
    Vec2 edge = {p2.x - p1.x, p2.y - p1.y};
    Vec2 axis = {-edge.y, edge.x}; // perpendicular

    float len = std::sqrt(axis.x * axis.x + axis.y * axis.y);
    if (len != 0.0f) {
        axis.x /= len;
        axis.y /= len;
    }
    return axis;
}

// Compute the four corners of a rectangle
static void getCorners(const Entity& e, Vec2 outCorners[4]) {
    float hw = e.size.x * 0.5f;
    float hh = e.size.y * 0.5f;
    float rad = deg2rad(e.rotation);

    float cosA = std::cos(rad);
    float sinA = std::sin(rad);

    // Local unrotated corners
    Vec2 local[4] = {
        {-hw, -hh},
        { hw, -hh},
        { hw,  hh},
        {-hw,  hh}
    };

    // Rotate + translate
    for (int i = 0; i < 4; ++i) {
        outCorners[i].x = e.pos.x + (local[i].x * cosA - local[i].y * sinA);
        outCorners[i].y = e.pos.y + (local[i].x * sinA + local[i].y * cosA);
    }
}

// Project rectangle corners onto an axis
static void projectOntoAxis(const Vec2 corners[4], const Vec2& axis,
                            float& min, float& max) {
    min = max = corners[0].x * axis.x + corners[0].y * axis.y;
    for (int i = 1; i < 4; ++i) {
        float projection = corners[i].x * axis.x + corners[i].y * axis.y;
        if (projection < min) min = projection;
        if (projection > max) max = projection;
    }
}

// ------------------------------------------------------------
// Entity::collidesWith
// ------------------------------------------------------------
bool Entity::intersects(const Entity& other) const {
    Vec2 cornersA[4];
    Vec2 cornersB[4];
    getCorners(*this, cornersA);
    getCorners(other, cornersB);

    // Axes to test: 2 unique from each rectangle
    Vec2 axes[4] = {
        edgeToAxis(cornersA[0], cornersA[1]),
        edgeToAxis(cornersA[1], cornersA[2]),
        edgeToAxis(cornersB[0], cornersB[1]),
        edgeToAxis(cornersB[1], cornersB[2]),
    };

    // SAT check: if projections do NOT overlap on any axis -> no collision
    for (int i = 0; i < 4; ++i) {
        float minA, maxA, minB, maxB;
        projectOntoAxis(cornersA, axes[i], minA, maxA);
        projectOntoAxis(cornersB, axes[i], minB, maxB);

        if (maxA < minB || maxB < minA)
            return false; // Found separating axis
    }

    return true; // No separating axis found -> collision
}*/


