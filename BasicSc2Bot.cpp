#include "BasicSc2Bot.h"
#include <sc2api/sc2_api.h>
#include <sc2api/sc2_interfaces.h>
#include <sc2api/sc2_typeenums.h>
#include <sc2api/sc2_unit.h>

using namespace sc2;

/*
# Windows
./BasicSc2Bot.exe -c -a zerg -d Hard -m CactusValleyLE.SC2Map

# Mac
./BasicSc2Bot -c -a zerg -d Hard -m CactusValleyLE.SC2Map
*/

void BasicSc2Bot::OnGameStart() {
	return;
}

void BasicSc2Bot::OnStep() {
	const ObservationInterface *observation = Observation();

	if (TryTrainOverlord()) // Always check if overlord needed before spawning more units
		return;
	TryBuildSpawningPool();     // Build a spawning pool for queen to be spawned
	InjectLarvae(); // Spawn larvas for other units to spawn

	int current_workers = observation->GetFoodWorkers();
	int max_workers_per_base = 16 + 3 + 3; // 16 for base workers, 3 for first vespense extractor, 3 for second vespense extractor
	int total_bases = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY).size() + GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR).size() + GetUnitsOfType(UNIT_TYPEID::ZERG_HIVE).size();
	int desired_workers = max_workers_per_base * total_bases;

	if (current_workers < desired_workers) { // Only spawn 22 Drones
		if (TrainUnitFromLarvae(ABILITY_ID::TRAIN_DRONE, 50))
			return;
	}
	TryBuildVespeneExtractor(); // Build a vespene extractor
	AssignWorkersToExtractors();
	TryUpgradeBase();

	TrainUnitFromLarvae(ABILITY_ID::TRAIN_ZERGLING, 50); // Keep spawning Zerglings once all the above is done
}

void BasicSc2Bot::OnUnitIdle(const Unit *unit) {
	switch (unit->unit_type.ToType()) {
	case UNIT_TYPEID::ZERG_DRONE: {
		const Unit *mineral_target = FindNearestMineralPatch(unit->pos);
		if (!mineral_target) {
			break;
		}
		Actions()->UnitCommand(unit, ABILITY_ID::SMART, mineral_target);
		break;
	}
	default:
		break;
	}
}

bool BasicSc2Bot::TryBuildInfestationPit() {
	return true;
}

bool BasicSc2Bot::TryUpgradeBase() {
	Units hatcheries = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY);
	Units lairs = GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR);

	for (const Unit *hatchery : hatcheries) {
		if (Observation()->GetMinerals() >= 150 && Observation()->GetVespene() >= 100) {
			if (!GetUnitsOfType(UNIT_TYPEID::ZERG_SPAWNINGPOOL).empty()) {
				Actions()->UnitCommand(hatchery, ABILITY_ID::MORPH_LAIR);
				return true;
			}
		}
	}

	for (const Unit *lair : lairs) {
		if (Observation()->GetMinerals() >= 200 && Observation()->GetVespene() >= 150) {
			if (!GetUnitsOfType(UNIT_TYPEID::ZERG_INFESTATIONPIT).empty()) {
				Actions()->UnitCommand(lair, ABILITY_ID::MORPH_HIVE);
				return true;
			}
		}
	}

	return false;
}

void BasicSc2Bot::AssignWorkersToExtractors() {
	Units extractors = GetUnitsOfType(UNIT_TYPEID::ZERG_EXTRACTOR);
	Units drones = GetUnitsOfType(UNIT_TYPEID::ZERG_DRONE);

	for (const Unit *extractor : extractors) {
		if (extractor->assigned_harvesters < 3) {
			if (!drones.empty()) {
				const Unit *drone = drones.back();
				Actions()->UnitCommand(drone, ABILITY_ID::SMART, extractor);
				drones.pop_back(); // Remove the assigned drone
			}
		}
	}
	return;
}

bool BasicSc2Bot::TryBuildVespeneExtractor() {
	Units drones = GetUnitsOfType(UNIT_TYPEID::ZERG_DRONE);
	if (drones.empty())
		return false;

	const Unit *drone = drones.front();
	const Unit *vespene_geyser = FindNearestVespenseGeyser(drone->pos);
	if (!vespene_geyser)
		return false;

	Actions()->UnitCommand(drone, ABILITY_ID::BUILD_EXTRACTOR, vespene_geyser);
	return true;
}

bool BasicSc2Bot::TrainUnitFromLarvae(ABILITY_ID unit_ability, int unit_cost) {
	Units larvas = GetUnitsOfType(UNIT_TYPEID::ZERG_LARVA);

	for (const auto &larva : larvas) {
		if (Observation()->GetMinerals() >= unit_cost) { // For each larva if enough minerals, perform the ability ex: (Train_drone, Train_overlord, Train_zerling, etc..)
			Actions()->UnitCommand(larva, unit_ability);
			return true;
		}
	}
	return false;
}

bool BasicSc2Bot::InjectLarvae() {
	Units hatcheries = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY);
	Units lairs = GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR);
	Units hives = GetUnitsOfType(UNIT_TYPEID::ZERG_HIVE);

	// Combine all base types into a single list
	hatcheries.insert(hatcheries.end(), lairs.begin(), lairs.end());
	hatcheries.insert(hatcheries.end(), hives.begin(), hives.end());

	for (const Unit *base : hatcheries) {
		if (!HasQueenAssigned(base)) {                                 // If no queen is assigned
			if (Observation()->GetMinerals() >= 150) {                 // Ensure enough minerals
				Actions()->UnitCommand(base, ABILITY_ID::TRAIN_QUEEN); // Train a queen
				return true;
			}
		}

		Units queens = GetUnitsOfType(UNIT_TYPEID::ZERG_QUEEN);
		for (const Unit *queen : queens) {
			if (queen->energy >= 25 && DistanceSquared2D(queen->pos, base->pos) < 10 * 10) {
				Actions()->UnitCommand(queen, ABILITY_ID::EFFECT_INJECTLARVA, base); // Inject larvae
				return true;
			}
		}
	}

	return false; // No actions performed
}

bool BasicSc2Bot::TryBuildSpawningPool() {
	if (!GetUnitsOfType(UNIT_TYPEID::ZERG_SPAWNINGPOOL).empty()) // Already exists
		return false;

	Units drones = GetUnitsOfType(UNIT_TYPEID::ZERG_DRONE);
	if (drones.empty() || Observation()->GetMinerals() < 200)
		return false;

	// Include Hatchery, Lair, and Hive for position selection
	Units bases = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY);
	Units lairs = GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR);
	Units hives = GetUnitsOfType(UNIT_TYPEID::ZERG_HIVE);

	bases.insert(bases.end(), lairs.begin(), lairs.end());
	bases.insert(bases.end(), hives.begin(), hives.end());

	if (bases.empty())
		return false;

	const Unit *base = bases.front();
	Point2D build_position = Point2D(base->pos.x + 5, base->pos.y + 5);

	const Unit *drone = drones.front();
	for (float x_offset = -2.0f; x_offset <= 2.0f; x_offset += 1.0f) {
		for (float y_offset = -2.0f; y_offset <= 2.0f; y_offset += 1.0f) {
			Point2D test_position = Point2D(build_position.x + x_offset, build_position.y + y_offset);
			if (Query()->Placement(ABILITY_ID::BUILD_SPAWNINGPOOL, test_position)) {
				Actions()->UnitCommand(drone, ABILITY_ID::BUILD_SPAWNINGPOOL, test_position);
				return true;
			}
		}
	}
	return false;
}

bool BasicSc2Bot::HasQueenAssigned(const Unit *base) {
	Units queens = GetUnitsOfType(UNIT_TYPEID::ZERG_QUEEN);
	for (const Unit *queen : queens) {
		if (DistanceSquared2D(base->pos, queen->pos) < 10 * 10)
			return true;
	}
	return false;
}

bool BasicSc2Bot::TryTrainOverlord() {
	const ObservationInterface *observation = Observation();
	if (observation->GetFoodUsed() <= observation->GetFoodCap() - 2) // Workers cap hasn't reached return false
		return false;

	Units larvas = GetUnitsOfType(UNIT_TYPEID::ZERG_LARVA);
	for (const auto &larva : larvas) {
		if (observation->GetMinerals() >= 100) {
			Actions()->UnitCommand(larva, ABILITY_ID::TRAIN_OVERLORD); // Checks for available larva and trains an overlord
			return true;
		}
	}
	return false; // All larvas are busy return false
}

const Unit *BasicSc2Bot::FindNearestMineralPatch(const Point2D &start) {
	Units units = Observation()->GetUnits(Unit::Alliance::Neutral);
	float distance = std::numeric_limits<float>::max();
	const Unit *target = nullptr;
	for (const auto &u : units) {
		if (u->unit_type == UNIT_TYPEID::NEUTRAL_MINERALFIELD) {
			float d = DistanceSquared2D(u->pos, start);
			if (d < distance) {
				distance = d;
				target = u;
			}
		}
	}
	return target;
}

const Unit *BasicSc2Bot::FindNearestVespenseGeyser(const Point2D &start) {
	Units units = Observation()->GetUnits(Unit::Alliance::Neutral);
	float distance = std::numeric_limits<float>::max();
	const Unit *target = nullptr;
	for (const auto &u : units) {
		if (u->unit_type == UNIT_TYPEID::NEUTRAL_VESPENEGEYSER) {
			float d = DistanceSquared2D(u->pos, start);
			if (d < distance) {
				distance = d;
				target = u;
			}
		}
	}
	return target;
}

Units BasicSc2Bot::GetUnitsOfType(UNIT_TYPEID type) {
	Units units = Observation()->GetUnits(Unit::Alliance::Self);
	Units units_vector;

	for (const auto &unit : units) {
		if (unit->unit_type == type) {    // If unit of the same type
			units_vector.push_back(unit); // Push to the vector
		}
	}

	return units_vector; // Return the vector
}
