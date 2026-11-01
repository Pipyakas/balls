#pragma once

#include "sim/PhysicsBackend.h"

#include <vector>

class ReferenceBackend final : public PhysicsBackend {
public:

    static constexpr double defaultSubStep = 0.01;

    explicit ReferenceBackend(double longestSubStep = defaultSubStep) : longestSubStep(longestSubStep) {}

    std::string_view name() const override { return "Reference"; }
    void uploadBoard(const BoardUpload& board) override;
    void addBalls(std::span<const BallSpawn> spawns) override;
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
    void removeBalls(std::span<const uint32_t> descendingIndices) override;
    size_t ballCount() const override { return balls.size(); }
    BallSnapshot snapshot() override;
    std::vector<BallSpawn> exportBalls() override;
    std::vector<BallSpawn> takeTail(size_t count) override;
    void clear() override { balls.clear(); }
    BallTally truncate(size_t newCount) override;

private:
    struct Ball {
        double x = 0.0;
        double y = 0.0;
        double vx = 0.0;
        double vy = 0.0;
        int color = 0;
        int bounces = 0;
    };

    int brickAt(int column, int row) const;
    int brickColor(int brick) const;

    int advance(Ball& ball, const StepParams& params) const;
    bool resolveContacts(Ball& ball, const StepParams& params, int& sameColorBrick) const;
    void retarget(Ball& ball, double normalX, double normalY, const StepParams& params) const;
    bool targetVisible(double x, double y, int targetColumn, int targetRow) const;

    std::vector<Ball> balls;
    std::vector<int32_t> cells;
    std::vector<int32_t> colors;
    std::vector<int32_t> targets;
    int columns = 0;
    int rows = 0;
    std::vector<uint32_t> packed;
    double longestSubStep;
};
