/*
 * Copyright 2005-2009, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "AGGTextRenderer.h"

#include <agg_basics.h>
#include <agg_bounding_rect.h>
#include <agg_conv_segmentator.h>
#include <agg_conv_stroke.h>
#include <agg_conv_transform.h>
#include <agg_path_storage.h>
#include <agg_scanline_boolean_algebra.h>
#include <agg_trans_affine.h>

#include <math.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#define SHOW_GLYPH_BOUNDS 0

#include "GlobalSubpixelSettings.h"
#include "GlyphLayoutEngine.h"
#include "IntRect.h"


AGGTextRenderer::AGGTextRenderer(renderer_subpix_type& subpixRenderer,
		renderer_type& solidRenderer, renderer_bin_type& binRenderer,
		scanline_unpacked_type& scanline,
		scanline_unpacked_subpix_type& subpixScanline,
		rasterizer_subpix_type& subpixRasterizer,
		scanline_unpacked_masked_type*& maskedScanline,
		agg::trans_affine& viewTransformation)
	:
	fPathAdaptor(),
	fGray8Adaptor(),
	fGray8Scanline(),
	fMonoAdaptor(),
	fMonoScanline(),
	fSubpixAdaptor(),

	fCurves(fPathAdaptor),
	fContour(fCurves),

	fSolidRenderer(solidRenderer),
	fBinRenderer(binRenderer),
	fSubpixRenderer(subpixRenderer),
	fScanline(scanline),
	fSubpixScanline(subpixScanline),
	fSubpixRasterizer(subpixRasterizer),
	fMaskedScanline(maskedScanline),

	fRasterizer(),

	fHinted(true),
	fAntialias(true),
	fEmbeddedTransformation(),
	fViewTransformation(viewTransformation)
{
	fCurves.approximation_scale(2.0);
	fContour.auto_detect_orientation(false);
}


AGGTextRenderer::~AGGTextRenderer()
{
}


void
AGGTextRenderer::SetFont(const ServerFont& font)
{
	fFont = font;

	// construct an embedded transformation (rotate & shear)
	fEmbeddedTransformation.Reset();
	fEmbeddedTransformation.ShearBy(B_ORIGIN,
		(90.0 - font.Shear()) * M_PI / 180.0, 0.0);
	fEmbeddedTransformation.RotateBy(B_ORIGIN,
		-font.Rotation() * M_PI / 180.0);

	fContour.width(font.FalseBoldWidth() * 2.0);
}


void
AGGTextRenderer::SetHinting(bool hinting)
{
	fHinted = hinting;
}


void
AGGTextRenderer::SetAntialiasing(bool antialiasing)
{
	if (fAntialias != antialiasing) {
		fAntialias = antialiasing;
		// NOTE: The fSubpixRasterizer is not used when anti-aliasing is
		// disbaled.
		if (!fAntialias)
			fRasterizer.gamma(agg::gamma_threshold(0.5));
		else
			fRasterizer.gamma(agg::gamma_power(1.0));
	}
}


typedef agg::conv_transform<FontCacheEntry::CurveConverter, Transformable>
	conv_font_trans_type;

typedef agg::conv_transform<FontCacheEntry::ContourConverter, Transformable>
	conv_font_contour_trans_type;



class AGGTextRenderer::StringRenderer {
public:
	StringRenderer(const IntRect& clippingFrame, bool dryRun,
			bool subpixelAntiAliased, bool underscore, bool strikeout,
			FontCacheEntry::TransformedOutline& transformedGlyph,
			FontCacheEntry::TransformedContourOutline& transformedContour,
			const Transformable& transform,
			const BPoint& transformOffset,
			BPoint* nextCharPos,
			AGGTextRenderer& renderer)
		:
		fTransform(transform),
		fTransformOffset(transformOffset),
		fClippingFrame(clippingFrame),
		fDryRun(dryRun),
		fSubpixelAntiAliased(subpixelAntiAliased),
		fVector(false),
		fUnderscore(underscore),
		fStrikeout(strikeout),
		fBounds(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN),
		fNextCharPos(nextCharPos),

		fTransformedGlyph(transformedGlyph),
		fTransformedContour(transformedContour),

		fRenderer(renderer)
	{
	}

	bool NeedsVector()
	{
		return !fTransform.IsTranslationOnly();
	}

	void Start()
	{
		fRenderer.fRasterizer.reset();
		fRenderer.fSubpixRasterizer.reset();
	}

	void Finish(double x, double y)
	{
		if (fVector) {
			if (fRenderer.fMaskedScanline != NULL) {
				agg::render_scanlines(fRenderer.fRasterizer,
					*fRenderer.fMaskedScanline, fRenderer.fSolidRenderer);
			} else if (fSubpixelAntiAliased) {
				agg::render_scanlines(fRenderer.fSubpixRasterizer,
					fRenderer.fSubpixScanline, fRenderer.fSubpixRenderer);
			} else {
				agg::render_scanlines(fRenderer.fRasterizer,
					fRenderer.fScanline, fRenderer.fSolidRenderer);
			}
		}

		if (fUnderscore && !fDryRun) {
			agg::path_storage path;
			IntRect bounds = fBounds;
			bounds.bottom = (int)y;
			bounds.OffsetBy(fTransformOffset);
			path.move_to(bounds.left + 0.5, bounds.bottom + 2.5);
			path.line_to(bounds.right + 0.5, bounds.bottom + 2.5);
			path.close_polygon();
			agg::conv_stroke<agg::path_storage> pathStorage(path);
			pathStorage.width(fRenderer.fFont.Size() / 12.0f);
			if (fRenderer.fMaskedScanline != NULL) {
				fRenderer.fRasterizer.add_path(pathStorage);
				agg::render_scanlines(fRenderer.fRasterizer,
					*fRenderer.fMaskedScanline, fRenderer.fSolidRenderer);
			} else if (fSubpixelAntiAliased) {
				fRenderer.fSubpixRasterizer.add_path(pathStorage);
				agg::render_scanlines(fRenderer.fSubpixRasterizer,
					fRenderer.fSubpixScanline, fRenderer.fSubpixRenderer);
			} else {
				fRenderer.fRasterizer.add_path(pathStorage);
				agg::render_scanlines(fRenderer.fRasterizer,
					fRenderer.fScanline, fRenderer.fSolidRenderer);
			}
		}

		if (fStrikeout && !fDryRun) {
			agg::path_storage path;
			font_height fontHeight;
			IntRect bounds = fBounds;
			fRenderer.fFont.GetHeight(fontHeight);
			float totalFontHeight = fontHeight.ascent + fontHeight.descent;
			bounds.bottom = (int)y;
			bounds.OffsetBy(fTransformOffset);
			path.move_to(bounds.left + 0.5, round(bounds.bottom - totalFontHeight * 0.25) + 0.5);
			path.line_to(bounds.right + 0.5, round(bounds.bottom - totalFontHeight * 0.25) + 0.5);
			path.close_polygon();
			agg::conv_stroke<agg::path_storage> pathStorage(path);
			pathStorage.width(fRenderer.fFont.Size() / 12.0f);
			if (fRenderer.fMaskedScanline != NULL) {
				fRenderer.fRasterizer.add_path(pathStorage);
				agg::render_scanlines(fRenderer.fRasterizer,
					*fRenderer.fMaskedScanline, fRenderer.fSolidRenderer);
			} else if (fSubpixelAntiAliased) {
				fRenderer.fSubpixRasterizer.add_path(pathStorage);
				agg::render_scanlines(fRenderer.fSubpixRasterizer,
					fRenderer.fSubpixScanline, fRenderer.fSubpixRenderer);
			} else {
				fRenderer.fRasterizer.add_path(pathStorage);
				agg::render_scanlines(fRenderer.fRasterizer,
					fRenderer.fScanline, fRenderer.fSolidRenderer);
			}
		}

		if (fNextCharPos) {
			fNextCharPos->x = x;
			fNextCharPos->y = y;
			fTransform.Transform(fNextCharPos);
		}
	}

	void ConsumeEmptyGlyph(int32 index, uint32 charCode, double x, double y)
	{
	}

	bool ConsumeGlyph(int32 index, uint32 charCode, const GlyphCache* glyph,
		FontCacheEntry* entry, double x, double y, double advanceX,
			double advanceY)
	{
		// "glyphBounds" is the bounds of the glyph transformed
		// by the x y location of the glyph along the base line,
		// it is therefor yet "untransformed" in case there is an
		// embedded transformation.
		const agg::rect_i& r = glyph->bounds;
		IntRect glyphBounds(int32(r.x1 + x), int32(r.y1 + y - 1),
			int32(r.x2 + x + 1), int32(r.y2 + y + 1));
			// NOTE: "-1"/"+1" converts the glyph bounding box from pixel
			// indices to pixel area coordinates

		// track bounding box
		if (glyphBounds.IsValid())
			fBounds = fBounds | glyphBounds;

		// render the glyph if this is not a dry run
		if (!fDryRun) {
			// init the fontmanager's embedded adaptors
			// NOTE: The initialization for the "location" of
			// the glyph is different depending on whether we
			// deal with non-(rotated/sheared) text, in which
			// case we have a native FT bitmap. For rotated or
			// sheared text, we use AGG vector outlines and
			// a transformation pipeline, which will be applied
			// _after_ we retrieve the outline, and that's why
			// we simply pass x and y, which are untransformed.

			// "glyphBounds" is now transformed into screen coords
			// in order to stop drawing when we are already outside
			// of the clipping frame
			if (glyph->data_type != glyph_data_outline) {
				// we cannot use the transformation pipeline
				double transformedX = x + fTransformOffset.x;
				double transformedY = y + fTransformOffset.y;
				entry->InitAdaptors(glyph, transformedX, transformedY,
					fRenderer.fMonoAdaptor,
					fRenderer.fGray8Adaptor,
					fRenderer.fPathAdaptor);

				glyphBounds.OffsetBy(fTransformOffset);
			} else {
				entry->InitAdaptors(glyph, x, y,
					fRenderer.fMonoAdaptor,
					fRenderer.fGray8Adaptor,
					fRenderer.fPathAdaptor);

				int32 falseBoldWidth = (int32)fRenderer.fContour.width();
				if (falseBoldWidth != 0)
					glyphBounds.InsetBy(-falseBoldWidth, -falseBoldWidth);
				// TODO: not correct! this is later used for clipping,
				// but it doesn't get the rect right
				glyphBounds = fTransform.TransformBounds(glyphBounds);
			}

			if (fClippingFrame.Intersects(glyphBounds)) {
				switch (glyph->data_type) {
					case glyph_data_mono:
						agg::render_scanlines(fRenderer.fMonoAdaptor,
							fRenderer.fMonoScanline, fRenderer.fBinRenderer);
						break;

					case glyph_data_gray8:
						if (fRenderer.fMaskedScanline != NULL) {
							agg::render_scanlines(fRenderer.fGray8Adaptor,
								*fRenderer.fMaskedScanline,
								fRenderer.fSolidRenderer);
						} else {
							agg::render_scanlines(fRenderer.fGray8Adaptor,
								fRenderer.fGray8Scanline,
								fRenderer.fSolidRenderer);
						}
						break;

					case glyph_data_subpix:
						// TODO: Handle alpha mask (fRenderer.fMaskedScanline)
						agg::render_scanlines(fRenderer.fGray8Adaptor,
							fRenderer.fGray8Scanline,
							fRenderer.fSubpixRenderer);
						break;

					case glyph_data_outline: {
						fVector = true;
						if (fSubpixelAntiAliased) {
							if (fRenderer.fContour.width() == 0.0) {
								fRenderer.fSubpixRasterizer.add_path(
									fTransformedGlyph);
							} else {
								fRenderer.fSubpixRasterizer.add_path(
									fTransformedContour);
							}
						} else {
							if (fRenderer.fContour.width() == 0.0) {
								fRenderer.fRasterizer.add_path(
									fTransformedGlyph);
							} else {
								fRenderer.fRasterizer.add_path(
									fTransformedContour);
							}
						}
#if SHOW_GLYPH_BOUNDS
	agg::path_storage p;
	p.move_to(glyphBounds.left + 0.5, glyphBounds.top + 0.5);
	p.line_to(glyphBounds.right + 0.5, glyphBounds.top + 0.5);
	p.line_to(glyphBounds.right + 0.5, glyphBounds.bottom + 0.5);
	p.line_to(glyphBounds.left + 0.5, glyphBounds.bottom + 0.5);
	p.close_polygon();
	agg::conv_stroke<agg::path_storage> ps(p);
	ps.width(1.0);
	if (fSubpixelAntiAliased) {
		fRenderer.fSubpixRasterizer.add_path(ps);
	} else {
		fRenderer.fRasterizer.add_path(ps);
	}
#endif

						break;
					}
					default:
						break;
				}
			}
		}
		return true;
	}

	IntRect Bounds() const
	{
		return fBounds;
	}

private:
	const Transformable& fTransform;
	const BPoint&		fTransformOffset;
	const IntRect&		fClippingFrame;
	bool				fDryRun;
	bool				fSubpixelAntiAliased;
	bool				fVector;
	bool				fUnderscore;
	bool				fStrikeout;
	IntRect				fBounds;
	BPoint*				fNextCharPos;

	FontCacheEntry::TransformedOutline& fTransformedGlyph;
	FontCacheEntry::TransformedContourOutline& fTransformedContour;
	AGGTextRenderer&	fRenderer;
};


BRect
AGGTextRenderer::RenderString(const char* string, uint32 length,
	const BPoint& baseLine, const BRect& clippingFrame, bool dryRun,
	BPoint* nextCharPos, const escapement_delta* delta,
	FontCacheReference* cacheReference)
{
//printf("RenderString(\"%s\", length: %ld, dry: %d)\n", string, length, dryRun);

	Transformable transform(fEmbeddedTransformation);
	transform.TranslateBy(baseLine);
	transform *= fViewTransformation;

	fCurves.approximation_scale(transform.scale());

	// use a transformation behind the curves
	// (only if glyph->data_type == agg::glyph_data_outline)
	// in the pipeline for the rasterizer
	FontCacheEntry::TransformedOutline
		transformedOutline(fCurves, transform);
	FontCacheEntry::TransformedContourOutline
		transformedContourOutline(fContour, transform);

	// for when we bypass the transformation pipeline
	BPoint transformOffset(0.0, 0.0);
	transform.Transform(&transformOffset);
	IntRect clippingIntFrame(clippingFrame);

	bool underscore = (fFont.Face() & B_UNDERSCORE_FACE) != 0;
	bool strikeout = (fFont.Face() & B_STRIKEOUT_FACE) != 0;

	StringRenderer renderer(clippingIntFrame, dryRun, gSubpixelAntialiasing && fAntialias,
		underscore, strikeout, transformedOutline, transformedContourOutline, transform,
		transformOffset, nextCharPos, *this);

	GlyphLayoutEngine::LayoutGlyphs(renderer, fFont, string, length, INT32_MAX,
		delta, fFont.Spacing(), NULL, cacheReference);

	return transform.TransformBounds(renderer.Bounds());
}


BRect
AGGTextRenderer::RenderString(const char* string, uint32 length,
	const BPoint* offsets, const BRect& clippingFrame, bool dryRun,
	BPoint* nextCharPos, FontCacheReference* cacheReference)
{
//printf("RenderString(\"%s\", length: %ld, dry: %d)\n", string, length, dryRun);

	Transformable transform(fEmbeddedTransformation);
	transform *= fViewTransformation;

	fCurves.approximation_scale(transform.scale());

	// use a transformation behind the curves
	// (only if glyph->data_type == agg::glyph_data_outline)
	// in the pipeline for the rasterizer
	FontCacheEntry::TransformedOutline
		transformedOutline(fCurves, transform);
	FontCacheEntry::TransformedContourOutline
		transformedContourOutline(fContour, transform);

	// for when we bypass the transformation pipeline
	BPoint transformOffset(0.0, 0.0);
	transform.Transform(&transformOffset);
	IntRect clippingIntFrame(clippingFrame);

	bool underscore = (fFont.Face() & B_UNDERSCORE_FACE) != 0;
	bool strikeout = (fFont.Face() & B_STRIKEOUT_FACE) != 0;

	StringRenderer renderer(clippingIntFrame, dryRun, gSubpixelAntialiasing && fAntialias,
		underscore, strikeout, transformedOutline, transformedContourOutline, transform,
		transformOffset, nextCharPos, *this);

	GlyphLayoutEngine::LayoutGlyphs(renderer, fFont, string, length, INT32_MAX,
		NULL, fFont.Spacing(), offsets, cacheReference);

	return transform.TransformBounds(renderer.Bounds());
}
