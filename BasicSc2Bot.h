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
	// Utility methods
	const Unit *FindNearestMineralPatch(const Point2D &start);
	const Unit *FindNearestHatcheryNeedingLarvae(const Point2D &start);
	Units GetUnitsOfType(UNIT_TYPEID type); // Retrieves units of the specified type
	int CountUnits(UNIT_TYPEID type);       // Counts units of the specified type

	// Zerg management methods
	bool TryBuildSpawningPool();                                      // Ensures a Spawning Pool is built
	bool TryTrainOverlord();                                          // Handles Zerg supply management
	bool TrySpawnLarvae();                                            // Manages larvae injection using Queens
	bool TrainUnitFromLarvae(ABILITY_ID unit_ability, int unit_cost); // Trains units from larvae

	// Queen management
	bool HasQueenAssigned(const Unit *hatchery); // Checks if a Queen is assigned to a Hatchery
	bool HasSpawningPool();                      // Checks if a Spawning Pool exists
};

#endif
