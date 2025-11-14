		// File: GameEngine.cpp
	// Description: Implements game loop coordination, envelope spawning, and timing.
	#include "backend/GameEngine.hpp"
	#include <algorithm>
	#include <cmath>
	#include <iostream>
	#include <stdexcept>
	#include <mutex>
	namespace backend {
	namespace {
	EnvelopeSize pickRandomSize(std::mt19937& rng) {
	    static std::discrete_distribution<int> distribution{50, 35, 15};
	    const int index = distribution(rng);
	    switch (index) {
	        case 0:
	            return EnvelopeSize::Small;
	        case 1:
	            return EnvelopeSize::Medium;
	        default:
	            return EnvelopeSize::Large;
	    }
	}
	int radiusForSize(EnvelopeSize size) {
	    switch (size) {
	        case EnvelopeSize::Small:
	            return 1;
	        case EnvelopeSize::Medium:
	            return 2;
	        case EnvelopeSize::Large:
	            return 3;
	    }
	    return 1;
	}
	int randomValueForSize(EnvelopeSize size, std::mt19937& rng) {
	    switch (size) {
	        case EnvelopeSize::Small: {
	            std::uniform_int_distribution<int> valueRange(5, 20);
	            return valueRange(rng);
	        }
	        case EnvelopeSize::Medium: {
	            std::uniform_int_distribution<int> valueRange(21, 60);
	            return valueRange(rng);
	        }
	        case EnvelopeSize::Large: {
	            std::uniform_int_distribution<int> valueRange(61, 120);
	            return valueRange(rng);
	        }
	    }
	    return 0;
	}
	}  // namespace
	// --- MODIFIED: Class Definition with new member ---
	class GameEngine {
	public:
	    GameEngine(GameConfig config);
	    void reset();
	    bool moveTank(MoveDirection direction);
	    bool isTimeUp() const;
	    double elapsedSeconds() const;
	    const Tank& getTank() const noexcept;
	    const std::vector<RedEnvelope>& getEnvelopes() const noexcept;
	    CollectionStats getStats() const noexcept;
	    const GameConfig& getConfig() const noexcept;
	    void setRandomSeed(unsigned int seed);
	    int spawnBonusEnvelopes(int minCount, int maxCount);
	    void pause();
	    void resume();
	    bool togglePause();
	    bool isPaused() const noexcept;
	    std::mutex& getMutex();
	private:
	    GameConfig m_config;
	    Tank m_tank;
	    std::vector<RedEnvelope> m_envelopes;
	    CollectionStats m_stats;
	    std::size_t m_nextEnvelopeId;
	    std::mt19937 m_rng;
	    // Pause and timing related members
	    bool m_paused = false;
	    std::chrono::steady_clock::time_point m_startTime;
	    std::chrono::steady_clock::time_point m_pauseStart;
	    std::chrono::duration<double> m_pausedAccumulated;
	    // --- ADDED: Mutex for thread safety ---
	    mutable std::mutex m_engineMutex;
	    RedEnvelope createRandomEnvelope(std::size_t id);
	    void respawnEnvelope(std::size_t index);
	    void handleCollisions();
	};
	// --- Implementations ---
	GameEngine::GameEngine(GameConfig config)
	    : m_config(config),
	      m_tank({config.worldWidth / 2, config.worldHeight / 2}),
	      m_rng(std::random_device{}()) {
	    if (config.worldWidth <= 0 || config.worldHeight <= 0) {
	        throw std::invalid_argument("World dimensions must be positive.");
	    }
	    if (config.initialEnvelopeCount <= 0) {
	        throw std::invalid_argument("At least one red envelope is required.");
	    }
	    if (config.timeLimitSeconds <= 0) {
	        throw std::invalid_argument("Time limit must be positive.");
	    }
	    reset();
	}
	void GameEngine::reset() {
	    std::lock_guard<std::mutex> lock(m_engineMutex);
	    std::cout << "GameEngine::reset start" << std::endl;
	    m_stats = {};
	    m_envelopes.clear();
	    m_envelopes.reserve(static_cast<std::size_t>(m_config.initialEnvelopeCount));
	    m_tank.setPosition({m_config.worldWidth / 2, m_config.worldHeight / 2});
	    m_nextEnvelopeId = 0;
	    // Reset pause-related state
	    m_paused = false;
	    m_pausedAccumulated = std::chrono::duration<double>{0.0};
	    m_pauseStart = {};
	    // Reset timing
	    m_startTime = std::chrono::steady_clock::now();
	    for (int i = 0; i < m_config.initialEnvelopeCount; ++i) {
	        std::cout << "Creating envelope " << i << std::endl;
	        m_envelopes.emplace_back(createRandomEnvelope(m_nextEnvelopeId++));
	    }
	    std::cout << "Before handleCollisions" << std::endl;
	    handleCollisions();
	    std::cout << "GameEngine::reset end" << std::endl;
	}
	bool GameEngine::moveTank(MoveDirection direction) {
	    std::lock_guard<std::mutex> lock(m_engineMutex);
	    if (isTimeUp() || m_paused) {
	        return false;
	    }
	    const bool moved = m_tank.move(direction, m_config.worldWidth, m_config.worldHeight);
	    handleCollisions();
	    return moved;
	}
	bool GameEngine::isTimeUp() const {
	    // This function calls elapsedSeconds which also needs a lock.
	    // To avoid deadlock, we implement the logic directly here.
	    std::lock_guard<std::mutex> lock(m_engineMutex);
	    if (m_startTime == std::chrono::steady_clock::time_point{}) {
	        return false;
	    }
	    auto now = std::chrono::steady_clock::now();
	    if (m_paused && m_pauseStart != std::chrono::steady_clock::time_point{}) {
	        now = m_pauseStart;
	    }
	    const std::chrono::duration<double> diff = now - m_startTime;
	    const double pausedSeconds = m_pausedAccumulated.count();
	    const double elapsed = std::max(0.0, diff.count() - pausedSeconds);
	    return elapsed >= static_cast<double>(m_config.timeLimitSeconds);
	}
	double GameEngine::elapsedSeconds() const {
	    // NOTE: This function is now only called internally by functions that already hold the lock.
	    // It is kept for completeness but WebServer will call isTimeUp() instead.
	    if (m_startTime == std::chrono::steady_clock::time_point{}) {
	        return 0.0;
	    }
	    auto now = std::chrono::steady_clock::now();
	    if (m_paused && m_pauseStart != std::chrono::steady_clock::time_point{}) {
	        now = m_pauseStart;
	    }
	    const std::chrono::duration<double> diff = now - m_startTime;
	    const double pausedSeconds = m_pausedAccumulated.count();
	    return std::max(0.0, diff.count() - pausedSeconds);
	}
	const Tank& GameEngine::getTank() const noexcept {
	    std::lock_guard<std::mutex> lock(m_engineMutex);
	    return m_tank;
	}
	const std::vector<RedEnvelope>& GameEngine::getEnvelopes() const noexcept {
	    std::lock_guard<std::mutex> lock(m_engineMutex);
	    return m_envelopes;
	}
	CollectionStats GameEngine::getStats() const noexcept {
	    std::lock_guard<std::mutex> lock(m_engineMutex);
	    return m_stats;
	}
	const GameConfig& GameEngine::getConfig() const noexcept {
	    std::lock_guard<std::mutex> lock(m_engineMutex);
	    return m_config;
	}
	void GameEngine::setRandomSeed(unsigned int seed) {
	    std::lock_guard<std::mutex> lock(m_engineMutex);
	    m_rng.seed(seed);
	}
	int GameEngine::spawnBonusEnvelopes(int minCount, int maxCount) {
	    std::lock_guard<std::mutex> lock(m_engineMutex);
	    if (minCount <= 0) {
	        minCount = 1;
	    }
	    if (maxCount < minCount) {
	        maxCount = minCount;
	    }
	    std::uniform_int_distribution<int> countDist(minCount, maxCount);
	    const int spawnCount = countDist(m_rng);
	    for (int i = 0; i < spawnCount; ++i) {
	        m_envelopes.emplace_back(createRandomEnvelope(m_nextEnvelopeId++));
	    }
	    return spawnCount;
	}
	void GameEngine::pause() {
	    std::lock_guard<std::mutex> lock(m_engineMutex);
	    if (m_paused) {
	        return;
	    }
	    m_paused = true;
	    m_pauseStart = std::chrono::steady_clock::now();
	}
	void GameEngine::resume() {
	    std::lock_guard<std::mutex> lock(m_engineMutex);
	    if (!m_paused) {
	        return;
	    }
	    const auto now = std::chrono::steady_clock::now();
	    if (m_pauseStart != std::chrono::steady_clock::time_point{}) {
	        m_pausedAccumulated += now - m_pauseStart;
	    }
	    m_paused = false;
	    m_pauseStart = {};
	}
	// --- FIXED: togglePause to avoid deadlock ---
	bool GameEngine::togglePause() {
	    std::lock_guard<std::mutex> lock(m_engineMutex);
	    if (m_paused) {
	        // Directly implement resume logic to avoid a second lock
	        const auto now = std::chrono::steady_clock::now();
	        if (m_pauseStart != std::chrono::steady_clock::time_point{}) {
	            m_pausedAccumulated += now - m_pauseStart;
	        }
	        m_paused = false;
	        m_pauseStart = {};
	    } else {
	        // Directly implement pause logic to avoid a second lock
	        m_paused = true;
	        m_pauseStart = std::chrono::steady_clock::now();
	    }
	    return m_paused;
	}
	bool GameEngine::isPaused() const noexcept {
	    std::lock_guard<std::mutex> lock(m_engineMutex);
	    return m_paused;
	}
	std::mutex& GameEngine::getMutex() {
	    return m_engineMutex;
	}
	RedEnvelope GameEngine::createRandomEnvelope(std::size_t id) {
	    std::uniform_int_distribution<int> widthDist(0, m_config.worldWidth - 1);
	    std::uniform_int_distribution<int> heightDist(0, m_config.worldHeight - 1);
	    EnvelopeSize size = pickRandomSize(m_rng);
	    Position position{widthDist(m_rng), heightDist(m_rng)};
	    const Position tankPos = m_tank.getPosition();
	    bool foundFreeSpot = false;
	    for (int attempts = 0; attempts < 150; ++attempts) {
	        position = {widthDist(m_rng), heightDist(m_rng)};
	        const int radius = radiusForSize(size);
	        const int dxTank = position.x - tankPos.x;
	        const int dyTank = position.y - tankPos.y;
	        if (dxTank * dxTank + dyTank * dyTank <= radius * radius) {
	            continue;
	        }
	        bool conflict = false;
	        for (const auto& envelope : m_envelopes) {
	            const Position existing = envelope.getPosition();
	            if (existing.x == position.x && existing.y == position.y) {
	                conflict = true;
	                break;
	            }
	        }
	        if (!conflict) {
	            foundFreeSpot = true;
	            break;
	        }
	    }
	    if (!foundFreeSpot) {
	        position = {widthDist(m_rng), heightDist(m_rng)};
	    }
	    return RedEnvelope(id,
	                       size,
	                       randomValueForSize(size, m_rng),
	                       position,
	                       radiusForSize(size));
	}
	void GameEngine::respawnEnvelope(std::size_t index) {
	    if (index >= m_envelopes.size()) {
	        return;
	    }
	    m_envelopes[index].setPosition({-10, -10});
	    m_envelopes[index] = createRandomEnvelope(m_nextEnvelopeId++);
	}
	void GameEngine::handleCollisions() {
	    for (std::size_t i = 0; i < m_envelopes.size(); ++i) {
	        if (isColliding(m_tank, m_envelopes[i])) {
	            m_stats.collectedCount += 1;
	            m_stats.collectedValue += m_envelopes[i].getValue();
	            respawnEnvelope(i);
	        }
	    }
	}
	}  // namespace backend