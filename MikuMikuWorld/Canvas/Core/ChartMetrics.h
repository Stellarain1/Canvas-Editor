#pragma once
#include "../../Score.h"
#include <json.hpp>
#include <map>
#include <string>
#include <vector>

// Objective numbers that describe how a chart is built: spacing, rhythm, movement, pattern kinds,
// repetition, density over the song, holds / traces / gimmicks. The same profile is taken from
// official charts (a reference per difficulty and level) and from generated ones, so a generated
// chart can be compared with what official charts of that difficulty actually do - metric by
// metric, not as one opaque number.
namespace Canvas::ChartMetrics
{
	// Histogram bins (shares summing to 1).
	using Histogram = std::vector<double>;

	struct Profile
	{
		double seconds{};					// note span
		int actions{}, moments{};
		std::map<std::string, double> scalars;
		std::map<std::string, Histogram> histograms;

		nlohmann::ordered_json toJson() const;
		static Profile fromJson(const nlohmann::json& json);
	};

	// Names and meaning of every metric, for reports and tools.
	const std::map<std::string, std::string>& scalarDescriptions();
	const std::map<std::string, std::string>& histogramDescriptions();

	Profile measure(const MikuMikuWorld::Score& score);

	// What official charts of one difficulty look like: every metric's spread over the charts, and
	// how the main ones change with the in-game level.
	struct Reference
	{
		struct Spread
		{
			double mean{}, sd{}, p10{}, p50{}, p90{};
			int count{};
		};
		struct LevelFit		// value ~ a + b * level, from charts with a known level
		{
			double a{}, b{};
			int count{};
			double at(double level) const { return a + b * level; }
		};
		int charts{};
		std::map<std::string, Spread> scalars;
		std::map<std::string, Histogram> histograms;	// average shares
		std::map<std::string, LevelFit> byLevel;
		double levelMin{}, levelMax{};

		// the expected value of a metric at a level (the plain average without level data)
		double expected(const std::string& metric, double level) const;
		nlohmann::ordered_json toJson() const;
		static Reference fromJson(const nlohmann::json& json);
	};

	// In-game level from a chart's numbers, fitted on official charts with known levels.
	struct LevelModel
	{
		double intercept{};
		std::vector<double> weights, means, scales;
		int charts{};
		double error{};		// root mean square error on the charts it was fitted on
		static const std::vector<std::string>& features();
		bool usable() const { return charts >= 12 && !weights.empty(); }
		double predict(const Profile& profile) const;
	};

	struct ReferenceSet
	{
		std::map<std::string, Reference> difficulties;
		LevelModel levels;
		std::string source;
		nlohmann::ordered_json toJson() const;
		static ReferenceSet fromJson(const nlohmann::json& json);
		const Reference* find(const std::string& difficulty) const;
	};

	// Builds references from profiled charts: (difficulty, level or 0, profile).
	struct Sample
	{
		std::string difficulty;
		double level{};
		Profile profile;
	};
	ReferenceSet buildReferences(const std::vector<Sample>& samples, const std::string& source);

	// Metric-by-metric comparison of a chart with a reference (at a level, 0 = typical).
	struct Deviation
	{
		std::string metric;
		double value{}, expected{}, z{};	// z: distance in official spreads (histograms: divergence x 10)
		bool histogram{};
	};
	struct Comparison
	{
		std::vector<Deviation> deviations;	// worst first
		double score{};						// mean of clipped deviations; 0 = typical official chart
		nlohmann::ordered_json toJson() const;
	};
	Comparison compare(const Profile& chart, const Reference& reference, double level = 0.0);
}
