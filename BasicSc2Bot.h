#ifndef BASICSC2_BOT_H
#define BASICSC2_BOT_H

#include "sc2api/sc2_api.h"
#include "sc2api/sc2_args.h"
#include "sc2lib/sc2_lib.h"
#include "sc2utils/sc2_arg_parser.h"
#include "sc2utils/sc2_manage_process.h"
#include <sc2api/sc2_typeenums.h>
#include <sc2api/sc2_unit.h>

using namespace sc2;

class BasicSc2Bot : public sc2::Agent {
  public:
	virtual void OnGameStart();
	virtual void OnStep();
	virtual void OnUnitIdle(const Unit *unit);

  private:
	// Utility methods
	const Unit *FindNearestMineralPatch(const Point2D &start);
	const Unit *FindNearestVespenseGeyser(const Point2D &start);
	Units GetUnitsOfType(UNIT_TYPEID type); // Retrieves units of the specified type

	// Zerg management methods
	void AssignWorkersToExtractors();                                                                                     // Assign workers to vespene extractor
	bool TryBuildVespeneExtractor();                                                                                      // Creates a Vespene Extractor at the closest location
	bool TryTrainOverlord();                                                                                              // Handles Zerg supply management
	bool InjectLarvae();                                                                                                  // Manages larvae injection using Queens
	bool TrainUnitFromLarvae(ABILITY_ID unit_ability, int mineral_cost, int vespene_cost = 0);                            // Trains units from larvae
	bool TryUpgradeBase();                                                                                                // For upgrading base to Liar, Hive
	bool TryBuildStructure(ABILITY_ID build_structure, UNIT_TYPEID structure_id, int mineral_cost, int vespene_cost = 0); // Build Structure

	// Queen management
	bool HasQueenAssigned(const Unit *hatchery); // Checks if a Queen is assigned to a Hatchery
	                                             // Scouting
	                                             // void CommandUnitScout(const Unit *unit);
};

#endif