#ifndef BASIC_SC2_BOT_H_
#define BASIC_SC2_BOT_H_

#include "sc2api/sc2_api.h"
#include "sc2api/sc2_args.h"
#include "sc2lib/sc2_lib.h"
#include "sc2utils/sc2_arg_parser.h"
#include "sc2utils/sc2_manage_process.h"

using namespace sc2;

class BasicSc2Bot : public sc2::Agent {
  public:
	virtual void OnGameStart() override;
	virtual void OnStep() override;
	virtual void OnUnitIdle(const Unit *unit) override;


private:

	struct MineralPatchesInfo{
		Point2D PatchLoc;
		float DistFromStart;
	};

	struct
	{
		bool operator()(BasicSc2Bot::MineralPatchesInfo lhs, BasicSc2Bot::MineralPatchesInfo rhs) const {
			return lhs.DistFromStart < rhs.DistFromStart;
		}
	} customLess;

	std::vector<MineralPatchesInfo> MineralPatches;
	Point2D SecondNearestLine;


	// Utility methods
	const Unit *FindNearestMineralPatch(const Point2D &start);
	const std::vector<Point2D> BasicSc2Bot::ListMineralPatches(const Point2D& start); // return list of known mineral patches
	const std::vector<MineralPatchesInfo> BasicSc2Bot::ListMineralPatchesInfo(const Point2D& start); // return list of known mineral patches
	Point2D BasicSc2Bot::FindNearestMineralLine(std::vector<BasicSc2Bot::MineralPatchesInfo> MineralPatchesInfo); // Find the nearest next Mineral Line to  start harvesting from it and making another hatcherty
	bool BasicSc2Bot::TryBuildSecondHatchery(); // Find an idle overlord and send it to nearest mineral line to build a hatchery
	Units GetUnitsOfType(UNIT_TYPEID type); // Retrieves units of the specified type

	// Zerg management methods
	bool TryBuildSpawningPool();                                      // Ensures a Spawning Pool is built
	bool TryTrainOverlord();                                          // Handles Zerg supply management
	bool TrySpawnLarvae();                                            // Manages larvae injection using Queens
	bool TrainUnitFromLarvae(ABILITY_ID unit_ability, int unit_cost); // Trains units from larvae

	// Queen management
	bool HasQueenAssigned(const Unit *hatchery); // Checks if a Queen is assigned to a Hatchery

	bool once = true;

};

#endif
