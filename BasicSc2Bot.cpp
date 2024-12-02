#include "BasicSc2Bot.h"
#include <sc2api/sc2_api.h>
#include <sc2api/sc2_typeenums.h>

using namespace sc2;

void BasicSc2Bot::OnGameStart() {
	expansions_ = search::CalculateExpansionLocations(Observation(), Query());
	startLocation_ = Observation()->GetStartLocation();
	return;
}

void BasicSc2Bot::OnStep() {
	const ObservationInterface *observation = Observation();

	// Check resources and expand if needed
	if (observation->GetMinerals() >= 300) {
		if (TryExpand(ABILITY_ID::BUILD_HATCHERY, UNIT_TYPEID::ZERG_DRONE)) {
			std::cout << "Hatchery expansion initiated.\n";
			return;
		}
	}

	if (TryTrainOverlord()) // Check if overlord is needed before spawning more units
		return;

	TryBuildVespeneExtractor(); // Build a Vespene Extractor
	AssignWorkersToExtractors();
	TryBuildSpawningPool(); // Build a spawning pool for queen production
	TrySpawnLarvae();       // Spawn larvae for unit production

	// Calculate desired workers dynamically
	int current_workers = observation->GetFoodWorkers();
	int desired_workers = GetExpectedWorkers();

	if (current_workers < desired_workers) { // Train drones if needed
		if (TrainUnitFromLarvae(ABILITY_ID::TRAIN_DRONE, 50))
			return;
	}

	// Train zerglings after all other priorities are met
	TrainUnitFromLarvae(ABILITY_ID::TRAIN_ZERGLING, 50);
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

bool BasicSc2Bot::TryExpand(AbilityID build_ability, UnitTypeID worker_type) {
	const ObservationInterface *observation = Observation();
	std::vector<std::pair<float, Point3D>> distances;

	// Calculate distances for all expansions
	for (const auto &expansion : expansions_) {
		float current_distance = Distance2D(startLocation_, expansion);
		if (current_distance > 1.0f) { // Skip current base location
			distances.push_back({current_distance, expansion});
		}
	}

	// Sort by distance
	std::sort(distances.begin(), distances.end(), [](const std::pair<float, Point3D> &a, const std::pair<float, Point3D> &b) { return a.first < b.first; });

	// Use the first three closest expansions
	for (size_t i = 0; i < std::min<size_t>(1, distances.size()); ++i) {
		const Point3D &expansion = distances[i].second;

		// Check placement and attempt to build
		if (Query()->Placement(build_ability, expansion)) {
			if (TryBuildStructure(build_ability, worker_type, expansion, true)) {
				std::cout << "Expanding to: (" << expansion.x << ", " << expansion.y << ")\n";
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

bool BasicSc2Bot::TryBuildStructure(AbilityID build_ability, UnitTypeID worker_type, const Point3D &location, bool check_placement) {
	const ObservationInterface *observation = Observation();

	// Check resources before proceeding
	if (observation->GetMinerals() < 300) {
		std::cout << "Not enough minerals to build structure.\n";
		return false;
	}

	// Get available workers
	Units workers = observation->GetUnits(
	    Unit::Self, [worker_type](const Unit &unit) { return unit.unit_type == worker_type && (unit.orders.empty() || unit.orders[0].ability_id == ABILITY_ID::HARVEST_GATHER); });

	std::cout << "Available workers: " << workers.size() << "\n";

	if (workers.empty()) {
		std::cerr << "No available workers to build structure.\n";
		return false;
	}

	// Use the first available worker
	const Unit *worker = workers.front();

	// Stop the worker and issue the build command
	Actions()->UnitCommand(worker, ABILITY_ID::STOP);
	Actions()->UnitCommand(worker, build_ability, location);
	return true;
}

void BasicSc2Bot::OnUnitIdle(const Unit *unit) {
	switch (unit->unit_type.ToType()) {
	case UNIT_TYPEID::ZERG_DRONE: { // If unit type is Drone
		const Unit *mineral_target = FindNearestMineralPatch(unit->pos);
		if (mineral_target) {
			Actions()->UnitCommand(unit, ABILITY_ID::SMART, mineral_target); // Make them harvest minerals on idle
		}
		break;
	}
	case UNIT_TYPEID::ZERG_QUEEN: { // Handle idle queens
		Units hatcheries = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY);
		for (const auto &hatchery : hatcheries) {
			if (hatchery->assigned_harvesters < hatchery->ideal_harvesters) {
				Actions()->UnitCommand(unit, ABILITY_ID::EFFECT_INJECTLARVA, hatchery);
				break;
			}
		}
		break;
	}
	default:
		break;
	}
}

int BasicSc2Bot::GetExpectedWorkers() {
	const ObservationInterface *observation = Observation();
	Units bases = GetUnitsOfType(UNIT_TYPEID::ZERG_HATCHERY);
	Units geysers = GetUnitsOfType(UNIT_TYPEID::NEUTRAL_VESPENEGEYSER);
	int expected_workers = 0;

	// Add workers for each completed base
	for (const auto &base : bases) {
		if (base->build_progress != 1) {
			continue;
		}
		expected_workers += base->ideal_harvesters;
	}

	// Add workers for each completed and active geyser
	for (const auto &geyser : geysers) {
		if (geyser->vespene_contents > 0 && geyser->build_progress == 1) {
			expected_workers += geyser->ideal_harvesters;
		}
	}

	return expected_workers;
}

bool BasicSc2Bot::TrainUnitFromLarvae(ABILITY_ID unit_ability, int unit_cost) {
	Units larvas = GetUnitsOfType(UNIT_TYPEID::ZERG_LARVA);

	for (const auto &larva : larvas) {
		if (Observation()->GetMinerals() >= unit_cost) { // For each larva if enough minerals perform the ability ex: (Train_drone, Train_overlord, etc..)
			Actions()->UnitCommand(larva, unit_ability);
			return true;
		}
	}
	return false;
}

bool BasicSc2Bot::TrySpawnLarvae() {
	if (GetUnitsOfType(UNIT_TYPEID::ZERG_SPAWNINGPOOL).empty()) // Spawning pool exists don't make another one
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
		if (DistanceSquared2D(hatchery->pos, queen->pos) < 10 * 10)
			return true;
	}
	for (const auto &order : hatchery->orders) {
		if (order.ability_id == ABILITY_ID::TRAIN_QUEEN)
			return true;
	}

	return false;
}

bool BasicSc2Bot::TryTrainOverlord() {
	const ObservationInterface *observation = Observation();

	// Stop training overlords if supply cap is at maximum (200)
	if (observation->GetFoodCap() >= 200) {
		std::cout << "Supply cap reached, no need for overlords.\n";
		return false;
	}

	// Check if supply is needed
	if (observation->GetFoodUsed() >= observation->GetFoodCap() - 2) {
		// Check if an overlord is already in production
		Units overlords = GetUnitsOfType(UNIT_TYPEID::ZERG_OVERLORD);
		for (const auto &overlord : overlords) {
			if (overlord->build_progress < 1.0f) {
				std::cout << "Overlord already in production.\n";
				return false;
			}
		}

		// Train an overlord if resources allow
		Units larvae = GetUnitsOfType(UNIT_TYPEID::ZERG_LARVA);
		if (!larvae.empty() && observation->GetMinerals() >= 100) {
			Actions()->UnitCommand(larvae.front(), ABILITY_ID::TRAIN_OVERLORD);
			std::cout << "Training a new overlord.\n";
			return true;
		}
	}

	// No overlord needed
	return false;
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

// // To ensure that we do not over or under saturate any base.
// void BasicSc2Bot::ManageWorkers(UNIT_TYPEID worker_type, AbilityID worker_gather_command, UNIT_TYPEID vespene_building_type) {
// 	const ObservationInterface *observation = Observation();
// 	Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
// 	Units geysers = observation->GetUnits(Unit::Alliance::Self, IsUnit(vespene_building_type));

// 	if (bases.empty()) {
// 		return;
// 	}

// 	for (const auto &base : bases) {
// 		// If we have already mined out or still building here skip the base.
// 		if (base->ideal_harvesters == 0 || base->build_progress != 1) {
// 			continue;
// 		}
// 		// if base is
// 		if (base->assigned_harvesters > base->ideal_harvesters) {
// 			Units workers = observation->GetUnits(Unit::Alliance::Self, IsUnit(worker_type));

// 			for (const auto &worker : workers) {
// 				if (!worker->orders.empty()) {
// 					if (worker->orders.front().target_unit_tag == base->tag) {
// 						// This should allow them to be picked up by mineidleworkers()
// 						MineIdleWorkers(worker, worker_gather_command, vespene_building_type);
// 						return;
// 					}
// 				}
// 			}
// 		}
// 	}
// 	Units workers = observation->GetUnits(Unit::Alliance::Self, IsUnit(worker_type));
// 	for (const auto &geyser : geysers) {
// 		if (geyser->ideal_harvesters == 0 || geyser->build_progress != 1) {
// 			continue;
// 		}
// 		if (geyser->assigned_harvesters > geyser->ideal_harvesters) {
// 			for (const auto &worker : workers) {
// 				if (!worker->orders.empty()) {
// 					if (worker->orders.front().target_unit_tag == geyser->tag) {
// 						// This should allow them to be picked up by mineidleworkers()
// 						MineIdleWorkers(worker, worker_gather_command, vespene_building_type);
// 						return;
// 					}
// 				}
// 			}
// 		} else if (geyser->assigned_harvesters < geyser->ideal_harvesters) {
// 			for (const auto &worker : workers) {
// 				if (!worker->orders.empty()) {
// 					// This should move a worker that isn't mining gas to gas
// 					const Unit *target = observation->GetUnit(worker->orders.front().target_unit_tag);
// 					if (target == nullptr) {
// 						continue;
// 					}
// 					if (target->unit_type != vespene_building_type) {
// 						// This should allow them to be picked up by mineidleworkers()
// 						MineIdleWorkers(worker, worker_gather_command, vespene_building_type);
// 						return;
// 					}
// 				}
// 			}
// 		}
// 	}
// }