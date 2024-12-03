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
	const Unit *FindNearestMineralPatch(const Point2D &start);
	const Unit *FindNearestVespenseGeyser(const Point2D &start);
	Units GetUnitsOfType(UNIT_TYPEID type); 								// Retrieves units of the specified type

	void AssignWorkersToExtractors();                                                                                     // Assign workers to vespene extractors
	bool TryBuildVespeneExtractor();                                                                                      // Creates a Vespene Extractor at the closest location
	bool TryTrainOverlord();                                                                                              // Handles Zerg supply management
	bool QueenInjectLarvae();                                                                                             // Manages larvae injection using Queens
	bool TrainUnitFromLarvae(ABILITY_ID unit_ability, int mineral_cost, int vespene_cost = 0);                            // Trains units from larvae
	bool TryUpgradeBase();                                                                                                // For upgrading base to Lair, Hive
	bool TryBuildStructure(ABILITY_ID build_structure, UNIT_TYPEID structure_id, int mineral_cost, int vespene_cost = 0); // Build Structure

	bool HasQueenAssigned(const Unit *base); // Checks if a Queen is assigned to a base

	std::vector<Point3D> expansions_;
	bool TryExpand(AbilityID build_ability, UnitTypeID worker_type);
	bool TryBuildStructure(AbilityID build_ability, UnitTypeID worker_type, const Point3D &location, bool check_placement);
	Point3D startLocation_;
	int GetExpectedWorkers();
	void BalanceWorkers(); 												// Balances workers among bases

	void ManageArmy();                        					// Function to manage army units and attack
	void AttackWithArmy();                    					// Function to order the army to attack
	bool TrainArmyUnits();                    					// Trains army units based on available tech structures
	void TryBuildTechStructuresAndUpgrades(); 					// Builds tech structures and researches upgrades
	Units GetActiveBases();                  					// Returns a list of active bases (Hatcheries, Lairs, Hives)
	int CountUnitType(UNIT_TYPEID unit_type);
	std::vector<Point2D> enemy_base_locations_; 				// Possible enemy base locations
	size_t current_target_index_;
	bool IsCombatUnit(const Unit &unit); 						// Helper function to check if a unit is a combat unit
	Point2D GetArmyRallyPoint();
};

#endif