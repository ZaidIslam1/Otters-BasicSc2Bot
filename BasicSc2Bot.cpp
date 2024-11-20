#include "BasicSc2Bot.h"
#include <sc2api/sc2_api.h>
#include <sc2api/sc2_typeenums.h>

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
	TryBuildVespeneExtractor(); // Build a vespene extractor
	AssignWorkersToExtractors();
	InjectLarvae(); // Spawn larvas for other units to spawn

	int current_workers = observation->GetFoodWorkers();
	int max_workers_per_hatchery = 15 + 3 + 3; // 16 for Hatchery workers, 3 for first vespense extractor, 3 for second vespense extractor
	int desired_workers = static_cast<int>(max_workers_per_hatchery * GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY).size());

	if (current_workers < desired_workers) { // Only spawn 22 Drones
		if (TrainUnitFromLarvae(ABILITY_ID::TRAIN_DRONE, 50))
			return;
	}

	TrainUnitFromLarvae(ABILITY_ID::TRAIN_ZERGLING, 50); // Keep spawning Zerglings once all the above is done
}

void BasicSc2Bot::OnUnitIdle(const Unit *unit) {
	switch (unit->unit_type.ToType()) {
	case UNIT_TYPEID::ZERG_DRONE: { // If unit type is Drone
		break;
	}
	default:
		break;
	}
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
	if (GetUnitsOfType(UNIT_TYPEID::ZERG_SPAWNINGPOOL).empty()) // Spawning pool exists don't make another one (Can't make queen without one)
		return false;

	Units hatcheries = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY);
	for (const auto &hatchery : hatcheries) {
		if (!HasQueenAssigned(hatchery)) {                                 // If no queen is created
			if (Observation()->GetMinerals() >= 150) {                     // Cost for queen
				Actions()->UnitCommand(hatchery, ABILITY_ID::TRAIN_QUEEN); // Hatchery trains a queen
				return true;
			}
		}

		Units queens = GetUnitsOfType(UNIT_TYPEID::ZERG_QUEEN);
		for (const auto &queen : queens) {
			if (queen->energy >= 25 && DistanceSquared2D(queen->pos, hatchery->pos) < 10 * 10) { // When queens has enough energy and queens are nearby a hatchery
				Actions()->UnitCommand(queen, ABILITY_ID::EFFECT_INJECTLARVA, hatchery);         // Spawn larva
			}
		}
	}
	return true;
}

bool BasicSc2Bot::TryBuildSpawningPool() {
	if (!GetUnitsOfType(UNIT_TYPEID::ZERG_SPAWNINGPOOL).empty()) // Spawning pool already exists return false
		return false;

	Units drones = GetUnitsOfType(UNIT_TYPEID::ZERG_DRONE); // No drones are trained or not enough minerals
	if (drones.empty() || Observation()->GetMinerals() < 200)
		return false;

	Units hatcheries = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY);
	const Unit *hatchery = hatcheries.front();                                  // Get first hatchery
	Point2D build_position = Point2D(hatchery->pos.x + 5, hatchery->pos.y + 5); // Assign a build position
	const Unit *drone = drones.front();                                         // Get the first available drone to spawn a spawning pool

	for (float x_offset = -2.0f; x_offset <= 2.0f; x_offset += 1.0f) { // Logic for finding a valid location to build
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

bool BasicSc2Bot::HasQueenAssigned(const Unit *hatchery) {
	Units queens = GetUnitsOfType(UNIT_TYPEID::ZERG_QUEEN);
	for (const auto &queen : queens) {
		if (DistanceSquared2D(hatchery->pos, queen->pos) < 10 * 10) // Checks if queen is nearby the hatchery within 10 units
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
