#include "MathUtils.h"

namespace MikuMikuWorld
{
	float lerp(float start, float end, float percentage)
	{
		return start + percentage * (end - start);
	}

	float unlerp(float start, float end, float value)
	{
		return (value - start) / (end - start);
	}

	double lerpD(double start, double end, double percentage)
	{
		return start + percentage * (end - start);
	}

	double unlerpD(double start, double end, double value)
	{
		return (value - start) / (end - start);
	}

	float easeIn(float start, float end, float ratio)
	{
		return lerp(start, end, ratio * ratio);
	}

	float easeOut(float start, float end, float ratio)
	{
		return lerp(start, end, 1 - (1 - ratio) * (1 - ratio));
	}

	// Same curves as MikuMikuWorld4CC / the Sonolus pjsekai engine: in for the first half
	// and out for the second (in-out), or the other way round (out-in).
	float easeInOut(float start, float end, float ratio)
	{
		const float t = ratio < 0.5f ? 2 * ratio * ratio : 1 - (1 - ratio) * (1 - ratio) * 2;
		return lerp(start, end, t);
	}

	float easeOutIn(float start, float end, float ratio)
	{
		const float t = 0.5f + (0.5f - ratio) * (0.5f - ratio) * (ratio < 0.5f ? -2.0f : 2.0f);
		return lerp(start, end, t);
	}

	float midpoint(float x1, float x2)
	{
		return (x1 + x2) * 0.5f;
	}

	bool isWithinRange(float x, float left, float right)
	{
		return x >= left && x <= right;
	}

	std::function<float(float, float, float)> getEaseFunction(EaseType ease)
	{
		switch (ease)
		{
		case EaseType::EaseIn:
			return easeIn;
		case EaseType::EaseOut:
			return easeOut;
		case EaseType::EaseInOut:
			return easeInOut;
		case EaseType::EaseOutIn:
			return easeOutIn;
		default:
			break;
		}

		return lerp;
	}

	uint32_t gcf(uint32_t a, uint32_t b)
	{
		for (;;)
		{
			if (b == 0)
			{
				break;
			}
			else
			{
				uint32_t t = a;
				a = b;
				b = t % a;
			}
		}

		return a;
	}

	float easeInCubic(float x) { return x * x * x; }
	float easeOutCubic(float x) { return 3 * x - 3 * x * x + x * x * x; }
}