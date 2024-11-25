#include "BasicSc2Bot.h"
#include <random>
#include <sc2api/sc2_api.h>
#include <sc2api/sc2_common.h>
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

	if (TryTrainOverlord()) // Train Overlord if needed
		return;

	TryBuildStructure(ABILITY_ID::BUILD_SPAWNINGPOOL, UNIT_TYPEID::ZERG_SPAWNINGPOOL, 200); // Build Spawning Pool
	QueenInjectLarvae();

	int current_workers = observation->GetFoodWorkers();
	int max_workers_per_base = 16 + 3 + 3; // 16 for base workers, 3 for first vespense extractor, 3 for second
	int total_bases = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY).size() + GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR).size() + GetUnitsOfType(UNIT_TYPEID::ZERG_HIVE).size();
	int desired_workers = max_workers_per_base * total_bases;

	if (current_workers < desired_workers) { // Train Drones if under desired count
		if (TrainUnitFromLarvae(ABILITY_ID::TRAIN_DRONE, 50))
			return;
	}

	TryBuildVespeneExtractor(); // Build a Vespene Extractor
	AssignWorkersToExtractors();

	Units spawning_pools = GetUnitsOfType(UNIT_TYPEID::ZERG_SPAWNINGPOOL);
	if (!spawning_pools.empty() && spawning_pools.front()->build_progress == 1.0) {
		TryBuildStructure(ABILITY_ID::BUILD_ROACHWARREN, UNIT_TYPEID::ZERG_ROACHWARREN, 150);
	}

	Units lairs = GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR);
	if (lairs.empty() && !GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY).empty()) {
		TryUpgradeBase();
	} else if (!lairs.empty() && lairs.front()->build_progress == 1.0) {
		Units hydralisk_dens = GetUnitsOfType(UNIT_TYPEID::ZERG_HYDRALISKDEN);
		if (hydralisk_dens.empty()) {
			if (TryBuildStructure(ABILITY_ID::BUILD_HYDRALISKDEN, UNIT_TYPEID::ZERG_HYDRALISKDEN, 100, 50)) {
				return; // Wait for the Hydralisk Den to start building
			}
		} else {
			if (hydralisk_dens.front()->build_progress < 1.0) {
				return; // Hydralisk Den still under construction, wait
			}
		}

		Units spires = GetUnitsOfType(UNIT_TYPEID::ZERG_SPIRE);
		if (spires.empty()) {
			if (TryBuildStructure(ABILITY_ID::BUILD_SPIRE, UNIT_TYPEID::ZERG_SPIRE, 200, 150)) {
				return; // Wait for the Spire to start building
			}
		} else {
			if (spires.front()->build_progress < 1.0) {
				return; // Spire still under construction, wait
			}
		}
	}

	if (GetUnitsOfType(UNIT_TYPEID::ZERG_ZERGLING).size() <= 25 && !GetUnitsOfType(UNIT_TYPEID::ZERG_SPAWNINGPOOL).empty()) {
		TrainUnitFromLarvae(ABILITY_ID::TRAIN_ZERGLING, 50);
	}
	if (GetUnitsOfType(UNIT_TYPEID::ZERG_ROACH).size() <= 25 && !GetUnitsOfType(UNIT_TYPEID::ZERG_ROACHWARREN).empty()) {
		TrainUnitFromLarvae(ABILITY_ID::TRAIN_ROACH, 75, 25);
	}
	if (GetUnitsOfType(UNIT_TYPEID::ZERG_HYDRALISK).size() <= 10 && !GetUnitsOfType(UNIT_TYPEID::ZERG_HYDRALISKDEN).empty()) {
		TrainUnitFromLarvae(ABILITY_ID::TRAIN_HYDRALISK, 100, 50);
	}
	if (GetUnitsOfType(UNIT_TYPEID::ZERG_MUTALISK).size() <= 20 && !GetUnitsOfType(UNIT_TYPEID::ZERG_SPIRE).empty()) {
		TrainUnitFromLarvae(ABILITY_ID::TRAIN_MUTALISK, 100, 100);
	}
	// Wait for Hive upgrade before building Infestation Pit
	Units hives = GetUnitsOfType(UNIT_TYPEID::ZERG_HIVE);
	if (hives.empty()) {
		TryBuildStructure(ABILITY_ID::BUILD_INFESTATIONPIT, UNIT_TYPEID::ZERG_INFESTATIONPIT, 100, 100);
	}
	TryUpgradeBase();
}

void BasicSc2Bot::OnUnitIdle(const Unit *unit) {
	switch (unit->unit_type.ToType()) {
	case UNIT_TYPEID::ZERG_DRONE: {
		const Unit *mineral_target = FindNearestMineralPatch(unit->pos + Point2D(100, 100));
		if (mineral_target) {
			Actions()->UnitCommand(unit, ABILITY_ID::SMART, mineral_target);
		}
		break;
	}
	case UNIT_TYPEID::ZERG_SPIRE: {
		Actions()->UnitCommand(unit, ABILITY_ID::RESEARCH_ZERGFLYERARMORLEVEL1); // Upgrades for all Air units
		Actions()->UnitCommand(unit, ABILITY_ID::RESEARCH_ZERGFLYERATTACKLEVEL1);
		break;
	}
	case UNIT_TYPEID::ZERG_HYDRALISKDEN: {
		Actions()->UnitCommand(unit, ABILITY_ID::RESEARCH_GROOVEDSPINES); // Upgrades for Hydralisks
		Actions()->UnitCommand(unit, ABILITY_ID::RESEARCH_MUSCULARAUGMENTS);
		break;
	}
	case UNIT_TYPEID::ZERG_SPAWNINGPOOL: {
		Actions()->UnitCommand(unit, ABILITY_ID::RESEARCH_ZERGLINGMETABOLICBOOST); // Upgrades for Zerlings
		Actions()->UnitCommand(unit, ABILITY_ID::RESEARCH_ZERGLINGADRENALGLANDS);
		break;
	}
	case UNIT_TYPEID::ZERG_OVERLORD: {
		// const GameInfo &game_info = Observation()->GetGameInfo();

		// std::vector<Point2D> scouting_points;
		// scouting_points.insert(scouting_points.end(), game_info.enemy_start_locations.begin(), game_info.enemy_start_locations.end());

		// Units mineral_fields = GetUnitsOfType(UNIT_TYPEID::NEUTRAL_MINERALFIELD);
		// for (const auto &mineral : mineral_fields) {
		// 	scouting_points.push_back(mineral->pos);
		// }
		// std::shuffle(scouting_points.begin(), scouting_points.end(), std::mt19937(std::random_device()()));
		// for (const Point2D &point : scouting_points) {
		// 	Actions()->UnitCommand(unit, ABILITY_ID::SMART, point);
		// 	break;
		// }
		break;
	}
	default:
		break;
	}
}

bool BasicSc2Bot::TryBuildStructure(ABILITY_ID build_structure, UNIT_TYPEID structure_id, int mineral_cost, int vespene_cost) {
	// Check if the structure already exists or is under construction
	if (!GetUnitsOfType(structure_id).empty()) {
		return false;
	}
	if (Observation()->GetMinerals() < mineral_cost || Observation()->GetVespene() < vespene_cost) {
		return false;
	}
	Units drones = GetUnitsOfType(UNIT_TYPEID::ZERG_DRONE);
	if (drones.empty()) {
		return false;
	}

	Units bases = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY);
	Units lairs = GetUnitsOfType(UNIT_TYPEID::ZERG_LAIR);
	Units hives = GetUnitsOfType(UNIT_TYPEID::ZERG_HIVE);

	bases.insert(bases.end(), lairs.begin(), lairs.end());
	bases.insert(bases.end(), hives.begin(), hives.end());

	if (bases.empty()) {
		return false;
	}
	const Unit *base = bases.front();
	const Unit *drone = drones.front();
	if (!drone) {
		return false;
	}

	// Starting position near the base
	Point2D base_position = base->pos;
	const float max_search_radius = 10.0f;    // Maximum radius to search for placement
	const float step_size = 1.0f;             // Step size for expanding the search
	const float min_structure_spacing = 3.0f; // Minimum spacing between structures

	// Search in all directions around the base
	for (float radius = 2.0f; radius <= max_search_radius; radius += step_size) {
		for (float x_offset = -radius; x_offset <= radius; x_offset += step_size) {
			for (float y_offset = -radius; y_offset <= radius; y_offset += step_size) {
				// Skip positions that are outside the circular search radius
				if (sqrt(x_offset * x_offset + y_offset * y_offset) > radius) {
					continue;
				}
				Point2D test_position = Point2D(base_position.x + x_offset, base_position.y + y_offset);
				bool is_too_close = false;
				for (const Unit *existing_structure : Observation()->GetUnits(Unit::Alliance::Self)) {
					if (DistanceSquared2D(test_position, existing_structure->pos) < min_structure_spacing * min_structure_spacing) {
						is_too_close = true;
						break;
					}
				}

				if (!is_too_close && Query()->Placement(build_structure, test_position)) {
					Actions()->UnitCommand(drone, build_structure, test_position);
					return true;
				}
			}
		}
	}

	return false;
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

bool BasicSc2Bot::TrainUnitFromLarvae(ABILITY_ID unit_ability, int mineral_cost, int vespene_cost) {
	Units larvas = GetUnitsOfType(UNIT_TYPEID::ZERG_LARVA);

	for (const auto &larva : larvas) {
		if (Observation()->GetMinerals() >= mineral_cost &&
		    Observation()->GetVespene() >= vespene_cost) { // For each larva if enough minerals, perform the ability ex: (Train_drone, Train_overlord, Train_zerling, etc..)
			Actions()->UnitCommand(larva, unit_ability);
			return true;
		}
	}
	return false;
}

bool BasicSc2Bot::QueenInjectLarvae() {
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

bool BasicSc2Bot::HasQueenAssigned(const Unit *base) {
	Units queens = GetUnitsOfType(UNIT_TYPEID::ZERG_QUEEN);

	for (const Unit *queen : queens) {
		if (DistanceSquared2D(base->pos, queen->pos) < 10 * 10) {
			return true;
		}
	}

	return false; // No queen assigned or being trained
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
	float closest_distance = std::numeric_limits<float>::max();
	const Unit *target = nullptr;
	for (const auto &u : units) {
		if ((u->unit_type == UNIT_TYPEID::NEUTRAL_MINERALFIELD || u->unit_type == UNIT_TYPEID::NEUTRAL_MINERALFIELD750) && u->mineral_contents > 0) {
			if (u->assigned_harvesters <= u->ideal_harvesters) {
				float distance = DistanceSquared2D(u->pos, start);
				if (distance < closest_distance) {
					closest_distance = distance;
					target = u;
				}
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
