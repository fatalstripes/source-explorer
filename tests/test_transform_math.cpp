// SPDX-License-Identifier: GPL-2.0-or-later
// Tests for core/transform-math: canvas-aware center / fit.
#include "core/transform-math.hpp"

#include <cmath>
#include <cstdio>

static int g_failed = 0, g_checks = 0;

static void near(const char *what, std::optional<double> actual, double expected, int line)
{
	++g_checks;
	if (!actual || std::abs(*actual - expected) > 1e-6) {
		++g_failed;
		std::printf("FAIL line %d: %s expected %g got %g\n", line, what, expected, actual ? *actual : -9999.0);
	}
}
#define NEAR(a, e) near(#a, (a), (e), __LINE__)

using namespace se;

static ItemGeometry hd()
{
	ItemGeometry g;
	g.srcW = 1920;
	g.srcH = 1080;
	return g;
}

int main()
{
	const Canvas land{1920, 1080}, vert{1080, 1920}, ultra{3440, 1440};

	{ // center, top-left anchor, vertical canvas
		auto p = centerOnCanvas(hd(), vert);
		NEAR(p.positionX, -420);
		NEAR(p.positionY, 420);
	}
	{ // center, already full-canvas landscape => origin
		auto p = centerOnCanvas(hd(), land);
		NEAR(p.positionX, 0);
		NEAR(p.positionY, 0);
	}
	{ // center-aligned item sits on the canvas center
		auto g = hd();
		g.alignment = align::Center;
		auto p = centerOnCanvas(g, ultra);
		NEAR(p.positionX, 1720);
		NEAR(p.positionY, 720);
	}
	{ // contain 16:9 into vertical canvas
		auto p = fitToCanvas(hd(), vert, FitMode::Contain);
		NEAR(p.scaleX, 0.5625);
		NEAR(p.scaleY, 0.5625);
		NEAR(p.positionX, 0);
		NEAR(p.positionY, 656.25);
	}
	{ // contain into ultrawide: height-limited, pillarboxed
		auto p = fitToCanvas(hd(), ultra, FitMode::Contain);
		NEAR(p.scaleX, 1440.0 / 1080.0);
		NEAR(p.positionX, (3440 - 1920 * 1440.0 / 1080.0) / 2);
		NEAR(p.positionY, 0);
	}
	{ // cover into vertical canvas: height-limited, overflows horizontally
		auto p = fitToCanvas(hd(), vert, FitMode::Cover);
		NEAR(p.scaleX, 1920.0 / 1080.0);
		NEAR(p.positionX, (1080 - 1920 * 1920.0 / 1080.0) / 2);
		NEAR(p.positionY, 0);
	}
	{ // stretch fills exactly
		auto p = fitToCanvas(hd(), vert, FitMode::Stretch);
		NEAR(p.scaleX, 1080.0 / 1920.0);
		NEAR(p.scaleY, 1920.0 / 1080.0);
		NEAR(p.positionX, 0);
		NEAR(p.positionY, 0);
	}
	{ // rotated 90 about top-left: contain fills vertical canvas, anchor at top-right
		auto g = hd();
		g.rotation = 90;
		auto p = fitToCanvas(g, vert, FitMode::Contain);
		NEAR(p.scaleX, 1.0);
		NEAR(p.positionX, 1080);
		NEAR(p.positionY, 0);
	}
	{ // rotated 90, stretch swaps axes
		auto g = hd();
		g.rotation = 90;
		auto p = fitToCanvas(g, vert, FitMode::Stretch);
		NEAR(p.scaleX, 1.0);
		NEAR(p.scaleY, 1.0);
	}
	{ // crop shrinks the effective size
		auto g = hd();
		g.cropLeft = 960; // 960x1080 visible
		auto p = fitToCanvas(g, land, FitMode::Contain);
		NEAR(p.scaleX, 1.0); // height-limited and width-limited both 1.0 vs 2.0 -> 1.0
		NEAR(p.positionX, 480);
		NEAR(p.positionY, 0);
	}
	{ // flip is preserved; flipped item extends left of its anchor
		auto g = hd();
		g.scaleX = -1.0;
		auto p = fitToCanvas(g, land, FitMode::Contain);
		NEAR(p.scaleX, -1.0);
		NEAR(p.positionX, 1920); // top-left anchor of a mirrored item sits at the right edge
		NEAR(p.positionY, 0);
	}
	{ // zero-size source (e.g. not yet rendered) yields an empty patch
		ItemGeometry g;
		auto p = fitToCanvas(g, land, FitMode::Contain);
		++g_checks;
		if (p.scaleX || p.positionX) {
			++g_failed;
			std::puts("FAIL zero-size should be empty");
		}
	}
	{ // bounds fit
		auto p = fitBoundsToCanvas(align::Center, vert);
		NEAR(p.boundsWidth, 1080);
		NEAR(p.boundsHeight, 1920);
		NEAR(p.positionX, 540);
		NEAR(p.positionY, 960);
		auto q = fitBoundsToCanvas(align::BottomRight, vert);
		NEAR(q.positionX, 1080);
		NEAR(q.positionY, 1920);
	}

	{ // rotate 90 about the center: TopLeft anchor at (100,50), visual center (1060,590)
		auto g = hd();
		g.posX = 100;
		g.posY = 50;
		auto c = visualCenter(g);
		NEAR(std::optional<double>(c.x), 1060);
		NEAR(std::optional<double>(c.y), 590);
		auto p = rotateAboutCenter(g, 90);
		NEAR(p.rotation, 90);
		NEAR(p.positionX, 1600);
		NEAR(p.positionY, -370);
	}
	{ // flip keeps the item in place: a mirrored TopLeft item is anchored at its right edge
		auto g = hd();
		g.posX = 100;
		g.posY = 50;
		auto h = flipAboutCenter(g, true);
		NEAR(h.scaleX, -1.0);
		NEAR(h.positionX, 2020);
		NEAR(h.positionY, 50);
		++g_checks;
		if (h.scaleY) {
			++g_failed;
			std::puts("FAIL horizontal flip must not touch scaleY");
		}
		auto v = flipAboutCenter(g, false);
		NEAR(v.scaleY, -1.0);
		NEAR(v.positionX, 100);
		NEAR(v.positionY, 1130);
		// Center-anchored items need no position change.
		g.alignment = align::Center;
		auto c = flipAboutCenter(g, true);
		NEAR(c.positionX, 100);
		NEAR(c.positionY, 50);
	}
	{ // normalization
		NEAR(std::optional<double>(normalizeDegrees(-90)), 270);
		NEAR(std::optional<double>(normalizeDegrees(450)), 90);
		NEAR(std::optional<double>(normalizeDegrees(360)), 0);
		NEAR(std::optional<double>(normalizeDegrees(0)), 0);
		auto g = hd();
		g.rotation = 270;
		NEAR(rotateAboutCenter(g, 180).rotation, 90);
		NEAR(rotateAboutCenter(g, -270).rotation, 0);
	}
	{ // property: rotate/flip never move the visual center, for every anchor, rotation,
	  // scale sign and crop
		const int aligns[] = {align::TopLeft, align::Top, align::TopRight, align::Left, align::Center,
				      align::Right, align::BottomLeft, align::Bottom, align::BottomRight};
		const double rots[] = {0, 30, 90, 200, 315};
		const double scales[] = {1.0, -1.0, 0.5, -2.0};
		int cases = 0;
		for (int a : aligns)
			for (double r : rots)
				for (double sx : scales)
					for (double sy : scales) {
						ItemGeometry g;
						g.srcW = 640;
						g.srcH = 360;
						g.cropLeft = 20;
						g.cropBottom = 10;
						g.alignment = a;
						g.rotation = r;
						g.scaleX = sx;
						g.scaleY = sy;
						g.posX = 123;
						g.posY = -45;
						const Point before = visualCenter(g);

						auto apply = [&](const TransformPatch &p) {
							ItemGeometry n = g;
							if (p.positionX) n.posX = *p.positionX;
							if (p.positionY) n.posY = *p.positionY;
							if (p.rotation) n.rotation = *p.rotation;
							if (p.scaleX) n.scaleX = *p.scaleX;
							if (p.scaleY) n.scaleY = *p.scaleY;
							return visualCenter(n);
						};
						for (const Point after : {apply(rotateAboutCenter(g, 90)), apply(rotateAboutCenter(g, -33)),
									  apply(flipAboutCenter(g, true)), apply(flipAboutCenter(g, false))}) {
							++g_checks;
							++cases;
							if (std::abs(after.x - before.x) > 1e-6 || std::abs(after.y - before.y) > 1e-6) {
								++g_failed;
								std::printf("FAIL center moved: align=%d rot=%g sx=%g sy=%g\n", a, r, sx, sy);
							}
						}
					}
		++g_checks;
		if (cases != 9 * 5 * 4 * 4 * 4) {
			++g_failed;
			std::puts("FAIL property test did not run every case");
		}
	}

	{ // size <-> scale
		ItemGeometry g;
		g.srcW = 1920;
		g.srcH = 1080;
		g.scaleX = 0.5;
		g.scaleY = 0.25;
		NEAR(std::optional<double>(sizeOf(g).w), 960);
		NEAR(std::optional<double>(sizeOf(g).h), 270);

		auto p = sizeToScalePatch(g, 1920, 540);
		NEAR(p.scaleX, 1.0);
		NEAR(p.scaleY, 0.5);

		g.scaleX = -0.5; // flipped item keeps its flip when resized
		NEAR(std::optional<double>(sizeOf(g).w), 960);
		NEAR(sizeToScalePatch(g, 480, 270).scaleX, -0.25);

		g.cropLeft = 960; // size is of the cropped rectangle
		NEAR(sizeToScalePatch(g, 960, 1080).scaleX, -1.0);

		ItemGeometry empty; // a source with no size yet (not rendered)
		++g_checks;
		if (sizeToScalePatch(empty, 100, 100).scaleX) {
			++g_failed;
			std::puts("FAIL zero-size source must give an empty patch");
		}
	}
	{ // centre on one axis only: 1920x540 clock, top-left anchor, on a vertical 1080x1920 canvas
		ItemGeometry g;
		g.srcW = 1920;
		g.srcH = 540;
		g.posX = 7;
		g.posY = 7;
		const Canvas vert{1080, 1920};
		auto h = centerHorizontally(g, vert);
		NEAR(h.positionX, -420);
		++g_checks;
		if (h.positionY) {
			++g_failed;
			std::puts("FAIL centre-horizontally must not touch Y");
		}
		auto v = centerVertically(g, vert);
		NEAR(v.positionY, 690);
		++g_checks;
		if (v.positionX) {
			++g_failed;
			std::puts("FAIL centre-vertically must not touch X");
		}
	}
	{ // copy / paste transform onto a source of a different native size
		ItemGeometry from;
		from.srcW = 1920;
		from.srcH = 1080;
		from.scaleX = -0.5; // flipped, 950x540 on canvas after the crop
		from.scaleY = 0.5;
		from.rotation = 10;
		from.alignment = align::Center;
		from.cropLeft = 20;
		from.posX = 100;
		from.posY = 50;
		const CopiedTransform c = copyTransform(from);
		NEAR(std::optional<double>(c.sizeW), 950); // (1920-20) * 0.5
		NEAR(std::optional<double>(c.sizeH), 540);

		ItemGeometry to;
		to.srcW = 1280;
		to.srcH = 720;
		const TransformPatch p = pasteTransform(c, to, false);
		NEAR(p.positionX, 100);
		NEAR(p.positionY, 50);
		NEAR(p.rotation, 10);
		NEAR(p.scaleX, -950.0 / 1260.0); // same on-canvas width, flip kept, crop applied first
		NEAR(p.scaleY, 540.0 / 720.0);
		++g_checks;
		if (!p.cropLeft || *p.cropLeft != 20 || !p.alignment || *p.alignment != align::Center) {
			++g_failed;
			std::puts("FAIL paste must carry crop and alignment");
		}
		++g_checks;
		if (p.boundsWidth) {
			++g_failed;
			std::puts("FAIL a non-bounds paste must not set bounds");
		}

		// Target sized by a bounding box: the size goes to the box, scale is left alone.
		const TransformPatch b = pasteTransform(c, to, true);
		NEAR(b.boundsWidth, 950);
		NEAR(b.boundsHeight, 540);
		++g_checks;
		if (b.scaleX) {
			++g_failed;
			std::puts("FAIL a bounds paste must not set scale");
		}

		// A target with no size yet: still pastes position/crop/etc, without a nonsense scale.
		ItemGeometry empty;
		const TransformPatch e = pasteTransform(c, empty, false);
		NEAR(e.positionX, 100);
		++g_checks;
		if (e.scaleX) {
			++g_failed;
			std::puts("FAIL zero-size target must not get a scale");
		}
	}
	{ // the Clock Widget case: 1920x540 browser source with its top-left at (0, 540)
		ItemGeometry g;
		g.srcW = 1920;
		g.srcH = 540;
		g.posX = 0;
		g.posY = 540;
		g.alignment = align::TopLeft;
		NEAR(std::optional<double>(visualCenter(g).x), 960);
		NEAR(std::optional<double>(visualCenter(g).y), 810);

		auto center = reanchorAboutCenter(g, align::Center);
		NEAR(center.positionX, 960); // Position now IS the source's centre
		NEAR(center.positionY, 810);
		++g_checks;
		if (!center.alignment || *center.alignment != align::Center) {
			++g_failed;
			std::puts("FAIL alignment missing from the re-anchor patch");
		}

		auto br = reanchorAboutCenter(g, align::BottomRight);
		NEAR(br.positionX, 1920);
		NEAR(br.positionY, 1080);
		auto top = reanchorAboutCenter(g, align::Top);
		NEAR(top.positionX, 960);
		NEAR(top.positionY, 540);
		auto left = reanchorAboutCenter(g, align::Left);
		NEAR(left.positionX, 0);
		NEAR(left.positionY, 810);
		// Re-anchoring to the anchor it already has changes nothing.
		auto same = reanchorAboutCenter(g, align::TopLeft);
		NEAR(same.positionX, 0);
		NEAR(same.positionY, 540);
	}
	{ // property: re-anchoring never moves the visual centre, for every combination of
	  // old anchor, new anchor, rotation, flips and crop; and it round-trips
		const int aligns[] = {align::TopLeft, align::Top, align::TopRight, align::Left, align::Center,
				      align::Right, align::BottomLeft, align::Bottom, align::BottomRight};
		const double rots[] = {0, 30, 90, 200, 315};
		const double scales[] = {1.0, -1.0, 0.5, -2.0};
		int cases = 0;
		for (int from : aligns)
			for (int to : aligns)
				for (double r : rots)
					for (double sx : scales)
						for (double sy : scales) {
							ItemGeometry g;
							g.srcW = 640;
							g.srcH = 360;
							g.cropRight = 30;
							g.cropTop = 12;
							g.alignment = from;
							g.rotation = r;
							g.scaleX = sx;
							g.scaleY = sy;
							g.posX = 87;
							g.posY = -310;
							const Point before = visualCenter(g);

							ItemGeometry n = g;
							const TransformPatch p = reanchorAboutCenter(g, to);
							n.alignment = *p.alignment;
							n.posX = *p.positionX;
							n.posY = *p.positionY;
							const Point after = visualCenter(n);
							++g_checks;
							++cases;
							if (std::abs(after.x - before.x) > 1e-6 || std::abs(after.y - before.y) > 1e-6) {
								++g_failed;
								std::printf("FAIL re-anchor moved centre: %d->%d rot=%g sx=%g sy=%g\n", from,
									    to, r, sx, sy);
							}

							// Going back to the original anchor restores the original position.
							const TransformPatch back = reanchorAboutCenter(n, from);
							++g_checks;
							if (std::abs(*back.positionX - g.posX) > 1e-6 ||
							    std::abs(*back.positionY - g.posY) > 1e-6) {
								++g_failed;
								std::printf("FAIL round trip: %d->%d rot=%g\n", from, to, r);
							}
						}
		++g_checks;
		if (cases != 9 * 9 * 5 * 4 * 4) {
			++g_failed;
			std::puts("FAIL re-anchor property test did not run every case");
		}
	}

	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
