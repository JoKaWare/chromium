/*
 * Copyright (C) 2012 Google, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY GOOGLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/frame/UseCounter.h"

#include "core/css/CSSStyleSheet.h"
#include "core/css/StyleSheetContents.h"
#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "core/frame/Deprecation.h"
#include "core/frame/FrameConsole.h"
#include "core/frame/FrameHost.h"
#include "core/frame/LocalFrame.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/workers/WorkerOrWorkletGlobalScope.h"
#include "platform/Histogram.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/weborigin/SchemeRegistry.h"

namespace {

int totalPagesMeasuredCSSSampleId() {
  return 1;
}

// Make sure update_use_counter_css.py was run which updates histograms.xml.
constexpr int kMaximumCSSSampleId = 555;

}  // namespace

namespace blink {

int UseCounter::mapCSSPropertyIdToCSSSampleIdForHistogram(
    CSSPropertyID cssPropertyID) {
  switch (cssPropertyID) {
    // Begin at 2, because 1 is reserved for totalPagesMeasuredCSSSampleId.
    case CSSPropertyColor:
      return 2;
    case CSSPropertyDirection:
      return 3;
    case CSSPropertyDisplay:
      return 4;
    case CSSPropertyFont:
      return 5;
    case CSSPropertyFontFamily:
      return 6;
    case CSSPropertyFontSize:
      return 7;
    case CSSPropertyFontStyle:
      return 8;
    case CSSPropertyFontVariant:
      return 9;
    case CSSPropertyFontWeight:
      return 10;
    case CSSPropertyTextRendering:
      return 11;
    case CSSPropertyAliasWebkitFontFeatureSettings:
      return 12;
    case CSSPropertyFontKerning:
      return 13;
    case CSSPropertyWebkitFontSmoothing:
      return 14;
    case CSSPropertyFontVariantLigatures:
      return 15;
    case CSSPropertyWebkitLocale:
      return 16;
    case CSSPropertyWebkitTextOrientation:
      return 17;
    case CSSPropertyWebkitWritingMode:
      return 18;
    case CSSPropertyZoom:
      return 19;
    case CSSPropertyLineHeight:
      return 20;
    case CSSPropertyBackground:
      return 21;
    case CSSPropertyBackgroundAttachment:
      return 22;
    case CSSPropertyBackgroundClip:
      return 23;
    case CSSPropertyBackgroundColor:
      return 24;
    case CSSPropertyBackgroundImage:
      return 25;
    case CSSPropertyBackgroundOrigin:
      return 26;
    case CSSPropertyBackgroundPosition:
      return 27;
    case CSSPropertyBackgroundPositionX:
      return 28;
    case CSSPropertyBackgroundPositionY:
      return 29;
    case CSSPropertyBackgroundRepeat:
      return 30;
    case CSSPropertyBackgroundRepeatX:
      return 31;
    case CSSPropertyBackgroundRepeatY:
      return 32;
    case CSSPropertyBackgroundSize:
      return 33;
    case CSSPropertyBorder:
      return 34;
    case CSSPropertyBorderBottom:
      return 35;
    case CSSPropertyBorderBottomColor:
      return 36;
    case CSSPropertyBorderBottomLeftRadius:
      return 37;
    case CSSPropertyBorderBottomRightRadius:
      return 38;
    case CSSPropertyBorderBottomStyle:
      return 39;
    case CSSPropertyBorderBottomWidth:
      return 40;
    case CSSPropertyBorderCollapse:
      return 41;
    case CSSPropertyBorderColor:
      return 42;
    case CSSPropertyBorderImage:
      return 43;
    case CSSPropertyBorderImageOutset:
      return 44;
    case CSSPropertyBorderImageRepeat:
      return 45;
    case CSSPropertyBorderImageSlice:
      return 46;
    case CSSPropertyBorderImageSource:
      return 47;
    case CSSPropertyBorderImageWidth:
      return 48;
    case CSSPropertyBorderLeft:
      return 49;
    case CSSPropertyBorderLeftColor:
      return 50;
    case CSSPropertyBorderLeftStyle:
      return 51;
    case CSSPropertyBorderLeftWidth:
      return 52;
    case CSSPropertyBorderRadius:
      return 53;
    case CSSPropertyBorderRight:
      return 54;
    case CSSPropertyBorderRightColor:
      return 55;
    case CSSPropertyBorderRightStyle:
      return 56;
    case CSSPropertyBorderRightWidth:
      return 57;
    case CSSPropertyBorderSpacing:
      return 58;
    case CSSPropertyBorderStyle:
      return 59;
    case CSSPropertyBorderTop:
      return 60;
    case CSSPropertyBorderTopColor:
      return 61;
    case CSSPropertyBorderTopLeftRadius:
      return 62;
    case CSSPropertyBorderTopRightRadius:
      return 63;
    case CSSPropertyBorderTopStyle:
      return 64;
    case CSSPropertyBorderTopWidth:
      return 65;
    case CSSPropertyBorderWidth:
      return 66;
    case CSSPropertyBottom:
      return 67;
    case CSSPropertyBoxShadow:
      return 68;
    case CSSPropertyBoxSizing:
      return 69;
    case CSSPropertyCaptionSide:
      return 70;
    case CSSPropertyClear:
      return 71;
    case CSSPropertyClip:
      return 72;
    case CSSPropertyAliasWebkitClipPath:
      return 73;
    case CSSPropertyContent:
      return 74;
    case CSSPropertyCounterIncrement:
      return 75;
    case CSSPropertyCounterReset:
      return 76;
    case CSSPropertyCursor:
      return 77;
    case CSSPropertyEmptyCells:
      return 78;
    case CSSPropertyFloat:
      return 79;
    case CSSPropertyFontStretch:
      return 80;
    case CSSPropertyHeight:
      return 81;
    case CSSPropertyImageRendering:
      return 82;
    case CSSPropertyLeft:
      return 83;
    case CSSPropertyLetterSpacing:
      return 84;
    case CSSPropertyListStyle:
      return 85;
    case CSSPropertyListStyleImage:
      return 86;
    case CSSPropertyListStylePosition:
      return 87;
    case CSSPropertyListStyleType:
      return 88;
    case CSSPropertyMargin:
      return 89;
    case CSSPropertyMarginBottom:
      return 90;
    case CSSPropertyMarginLeft:
      return 91;
    case CSSPropertyMarginRight:
      return 92;
    case CSSPropertyMarginTop:
      return 93;
    case CSSPropertyMaxHeight:
      return 94;
    case CSSPropertyMaxWidth:
      return 95;
    case CSSPropertyMinHeight:
      return 96;
    case CSSPropertyMinWidth:
      return 97;
    case CSSPropertyOpacity:
      return 98;
    case CSSPropertyOrphans:
      return 99;
    case CSSPropertyOutline:
      return 100;
    case CSSPropertyOutlineColor:
      return 101;
    case CSSPropertyOutlineOffset:
      return 102;
    case CSSPropertyOutlineStyle:
      return 103;
    case CSSPropertyOutlineWidth:
      return 104;
    case CSSPropertyOverflow:
      return 105;
    case CSSPropertyOverflowWrap:
      return 106;
    case CSSPropertyOverflowX:
      return 107;
    case CSSPropertyOverflowY:
      return 108;
    case CSSPropertyPadding:
      return 109;
    case CSSPropertyPaddingBottom:
      return 110;
    case CSSPropertyPaddingLeft:
      return 111;
    case CSSPropertyPaddingRight:
      return 112;
    case CSSPropertyPaddingTop:
      return 113;
    case CSSPropertyPage:
      return 114;
    case CSSPropertyPageBreakAfter:
      return 115;
    case CSSPropertyPageBreakBefore:
      return 116;
    case CSSPropertyPageBreakInside:
      return 117;
    case CSSPropertyPointerEvents:
      return 118;
    case CSSPropertyPosition:
      return 119;
    case CSSPropertyQuotes:
      return 120;
    case CSSPropertyResize:
      return 121;
    case CSSPropertyRight:
      return 122;
    case CSSPropertySize:
      return 123;
    case CSSPropertySrc:
      return 124;
    case CSSPropertySpeak:
      return 125;
    case CSSPropertyTableLayout:
      return 126;
    case CSSPropertyTabSize:
      return 127;
    case CSSPropertyTextAlign:
      return 128;
    case CSSPropertyTextDecoration:
      return 129;
    case CSSPropertyTextIndent:
      return 130;
    /* Removed CSSPropertyTextLineThrough* - 131-135 */
    case CSSPropertyTextOverflow:
      return 136;
    /* Removed CSSPropertyTextOverline* - 137-141 */
    case CSSPropertyTextShadow:
      return 142;
    case CSSPropertyTextTransform:
      return 143;
    /* Removed CSSPropertyTextUnderline* - 144-148 */
    case CSSPropertyTop:
      return 149;
    case CSSPropertyTransition:
      return 150;
    case CSSPropertyTransitionDelay:
      return 151;
    case CSSPropertyTransitionDuration:
      return 152;
    case CSSPropertyTransitionProperty:
      return 153;
    case CSSPropertyTransitionTimingFunction:
      return 154;
    case CSSPropertyUnicodeBidi:
      return 155;
    case CSSPropertyUnicodeRange:
      return 156;
    case CSSPropertyVerticalAlign:
      return 157;
    case CSSPropertyVisibility:
      return 158;
    case CSSPropertyWhiteSpace:
      return 159;
    case CSSPropertyWidows:
      return 160;
    case CSSPropertyWidth:
      return 161;
    case CSSPropertyWordBreak:
      return 162;
    case CSSPropertyWordSpacing:
      return 163;
    case CSSPropertyWordWrap:
      return 164;
    case CSSPropertyZIndex:
      return 165;
    case CSSPropertyAliasWebkitAnimation:
      return 166;
    case CSSPropertyAliasWebkitAnimationDelay:
      return 167;
    case CSSPropertyAliasWebkitAnimationDirection:
      return 168;
    case CSSPropertyAliasWebkitAnimationDuration:
      return 169;
    case CSSPropertyAliasWebkitAnimationFillMode:
      return 170;
    case CSSPropertyAliasWebkitAnimationIterationCount:
      return 171;
    case CSSPropertyAliasWebkitAnimationName:
      return 172;
    case CSSPropertyAliasWebkitAnimationPlayState:
      return 173;
    case CSSPropertyAliasWebkitAnimationTimingFunction:
      return 174;
    case CSSPropertyWebkitAppearance:
      return 175;
    // CSSPropertyWebkitAspectRatio was 176
    case CSSPropertyAliasWebkitBackfaceVisibility:
      return 177;
    case CSSPropertyWebkitBackgroundClip:
      return 178;
    // case CSSPropertyWebkitBackgroundComposite: return 179;
    case CSSPropertyWebkitBackgroundOrigin:
      return 180;
    case CSSPropertyAliasWebkitBackgroundSize:
      return 181;
    case CSSPropertyWebkitBorderAfter:
      return 182;
    case CSSPropertyWebkitBorderAfterColor:
      return 183;
    case CSSPropertyWebkitBorderAfterStyle:
      return 184;
    case CSSPropertyWebkitBorderAfterWidth:
      return 185;
    case CSSPropertyWebkitBorderBefore:
      return 186;
    case CSSPropertyWebkitBorderBeforeColor:
      return 187;
    case CSSPropertyWebkitBorderBeforeStyle:
      return 188;
    case CSSPropertyWebkitBorderBeforeWidth:
      return 189;
    case CSSPropertyWebkitBorderEnd:
      return 190;
    case CSSPropertyWebkitBorderEndColor:
      return 191;
    case CSSPropertyWebkitBorderEndStyle:
      return 192;
    case CSSPropertyWebkitBorderEndWidth:
      return 193;
    // CSSPropertyWebkitBorderFit was 194
    case CSSPropertyWebkitBorderHorizontalSpacing:
      return 195;
    case CSSPropertyWebkitBorderImage:
      return 196;
    case CSSPropertyAliasWebkitBorderRadius:
      return 197;
    case CSSPropertyWebkitBorderStart:
      return 198;
    case CSSPropertyWebkitBorderStartColor:
      return 199;
    case CSSPropertyWebkitBorderStartStyle:
      return 200;
    case CSSPropertyWebkitBorderStartWidth:
      return 201;
    case CSSPropertyWebkitBorderVerticalSpacing:
      return 202;
    case CSSPropertyWebkitBoxAlign:
      return 203;
    case CSSPropertyWebkitBoxDirection:
      return 204;
    case CSSPropertyWebkitBoxFlex:
      return 205;
    case CSSPropertyWebkitBoxFlexGroup:
      return 206;
    case CSSPropertyWebkitBoxLines:
      return 207;
    case CSSPropertyWebkitBoxOrdinalGroup:
      return 208;
    case CSSPropertyWebkitBoxOrient:
      return 209;
    case CSSPropertyWebkitBoxPack:
      return 210;
    case CSSPropertyWebkitBoxReflect:
      return 211;
    case CSSPropertyAliasWebkitBoxShadow:
      return 212;
    // CSSPropertyWebkitColumnAxis was 214
    case CSSPropertyWebkitColumnBreakAfter:
      return 215;
    case CSSPropertyWebkitColumnBreakBefore:
      return 216;
    case CSSPropertyWebkitColumnBreakInside:
      return 217;
    case CSSPropertyAliasWebkitColumnCount:
      return 218;
    case CSSPropertyAliasWebkitColumnGap:
      return 219;
    // CSSPropertyWebkitColumnProgression was 220
    case CSSPropertyAliasWebkitColumnRule:
      return 221;
    case CSSPropertyAliasWebkitColumnRuleColor:
      return 222;
    case CSSPropertyAliasWebkitColumnRuleStyle:
      return 223;
    case CSSPropertyAliasWebkitColumnRuleWidth:
      return 224;
    case CSSPropertyAliasWebkitColumnSpan:
      return 225;
    case CSSPropertyAliasWebkitColumnWidth:
      return 226;
    case CSSPropertyAliasWebkitColumns:
      return 227;
    // 228 was CSSPropertyWebkitBoxDecorationBreak (duplicated due to #ifdef).
    // 229 was CSSPropertyWebkitFilter (duplicated due to #ifdef).
    case CSSPropertyAlignContent:
      return 230;
    case CSSPropertyAlignItems:
      return 231;
    case CSSPropertyAlignSelf:
      return 232;
    case CSSPropertyFlex:
      return 233;
    case CSSPropertyFlexBasis:
      return 234;
    case CSSPropertyFlexDirection:
      return 235;
    case CSSPropertyFlexFlow:
      return 236;
    case CSSPropertyFlexGrow:
      return 237;
    case CSSPropertyFlexShrink:
      return 238;
    case CSSPropertyFlexWrap:
      return 239;
    case CSSPropertyJustifyContent:
      return 240;
    case CSSPropertyWebkitFontSizeDelta:
      return 241;
    case CSSPropertyGridTemplateColumns:
      return 242;
    case CSSPropertyGridTemplateRows:
      return 243;
    case CSSPropertyGridColumnStart:
      return 244;
    case CSSPropertyGridColumnEnd:
      return 245;
    case CSSPropertyGridRowStart:
      return 246;
    case CSSPropertyGridRowEnd:
      return 247;
    case CSSPropertyGridColumn:
      return 248;
    case CSSPropertyGridRow:
      return 249;
    case CSSPropertyGridAutoFlow:
      return 250;
    case CSSPropertyWebkitHighlight:
      return 251;
    case CSSPropertyWebkitHyphenateCharacter:
      return 252;
    // case CSSPropertyWebkitLineBoxContain: return 257;
    // case CSSPropertyWebkitLineAlign: return 258;
    case CSSPropertyWebkitLineBreak:
      return 259;
    case CSSPropertyWebkitLineClamp:
      return 260;
    // case CSSPropertyWebkitLineGrid: return 261;
    // case CSSPropertyWebkitLineSnap: return 262;
    case CSSPropertyWebkitLogicalWidth:
      return 263;
    case CSSPropertyWebkitLogicalHeight:
      return 264;
    case CSSPropertyWebkitMarginAfterCollapse:
      return 265;
    case CSSPropertyWebkitMarginBeforeCollapse:
      return 266;
    case CSSPropertyWebkitMarginBottomCollapse:
      return 267;
    case CSSPropertyWebkitMarginTopCollapse:
      return 268;
    case CSSPropertyWebkitMarginCollapse:
      return 269;
    case CSSPropertyWebkitMarginAfter:
      return 270;
    case CSSPropertyWebkitMarginBefore:
      return 271;
    case CSSPropertyWebkitMarginEnd:
      return 272;
    case CSSPropertyWebkitMarginStart:
      return 273;
    // CSSPropertyWebkitMarquee was 274.
    // CSSPropertyInternalMarquee* were 275-279.
    case CSSPropertyWebkitMask:
      return 280;
    case CSSPropertyWebkitMaskBoxImage:
      return 281;
    case CSSPropertyWebkitMaskBoxImageOutset:
      return 282;
    case CSSPropertyWebkitMaskBoxImageRepeat:
      return 283;
    case CSSPropertyWebkitMaskBoxImageSlice:
      return 284;
    case CSSPropertyWebkitMaskBoxImageSource:
      return 285;
    case CSSPropertyWebkitMaskBoxImageWidth:
      return 286;
    case CSSPropertyWebkitMaskClip:
      return 287;
    case CSSPropertyWebkitMaskComposite:
      return 288;
    case CSSPropertyWebkitMaskImage:
      return 289;
    case CSSPropertyWebkitMaskOrigin:
      return 290;
    case CSSPropertyWebkitMaskPosition:
      return 291;
    case CSSPropertyWebkitMaskPositionX:
      return 292;
    case CSSPropertyWebkitMaskPositionY:
      return 293;
    case CSSPropertyWebkitMaskRepeat:
      return 294;
    case CSSPropertyWebkitMaskRepeatX:
      return 295;
    case CSSPropertyWebkitMaskRepeatY:
      return 296;
    case CSSPropertyWebkitMaskSize:
      return 297;
    case CSSPropertyWebkitMaxLogicalWidth:
      return 298;
    case CSSPropertyWebkitMaxLogicalHeight:
      return 299;
    case CSSPropertyWebkitMinLogicalWidth:
      return 300;
    case CSSPropertyWebkitMinLogicalHeight:
      return 301;
    // WebkitNbspMode has been deleted, was return 302;
    case CSSPropertyOrder:
      return 303;
    case CSSPropertyWebkitPaddingAfter:
      return 304;
    case CSSPropertyWebkitPaddingBefore:
      return 305;
    case CSSPropertyWebkitPaddingEnd:
      return 306;
    case CSSPropertyWebkitPaddingStart:
      return 307;
    case CSSPropertyAliasWebkitPerspective:
      return 308;
    case CSSPropertyAliasWebkitPerspectiveOrigin:
      return 309;
    case CSSPropertyWebkitPerspectiveOriginX:
      return 310;
    case CSSPropertyWebkitPerspectiveOriginY:
      return 311;
    case CSSPropertyWebkitPrintColorAdjust:
      return 312;
    case CSSPropertyWebkitRtlOrdering:
      return 313;
    case CSSPropertyWebkitRubyPosition:
      return 314;
    case CSSPropertyWebkitTextCombine:
      return 315;
    case CSSPropertyWebkitTextDecorationsInEffect:
      return 316;
    case CSSPropertyWebkitTextEmphasis:
      return 317;
    case CSSPropertyWebkitTextEmphasisColor:
      return 318;
    case CSSPropertyWebkitTextEmphasisPosition:
      return 319;
    case CSSPropertyWebkitTextEmphasisStyle:
      return 320;
    case CSSPropertyWebkitTextFillColor:
      return 321;
    case CSSPropertyWebkitTextSecurity:
      return 322;
    case CSSPropertyWebkitTextStroke:
      return 323;
    case CSSPropertyWebkitTextStrokeColor:
      return 324;
    case CSSPropertyWebkitTextStrokeWidth:
      return 325;
    case CSSPropertyAliasWebkitTransform:
      return 326;
    case CSSPropertyAliasWebkitTransformOrigin:
      return 327;
    case CSSPropertyWebkitTransformOriginX:
      return 328;
    case CSSPropertyWebkitTransformOriginY:
      return 329;
    case CSSPropertyWebkitTransformOriginZ:
      return 330;
    case CSSPropertyAliasWebkitTransformStyle:
      return 331;
    case CSSPropertyAliasWebkitTransition:
      return 332;
    case CSSPropertyAliasWebkitTransitionDelay:
      return 333;
    case CSSPropertyAliasWebkitTransitionDuration:
      return 334;
    case CSSPropertyAliasWebkitTransitionProperty:
      return 335;
    case CSSPropertyAliasWebkitTransitionTimingFunction:
      return 336;
    case CSSPropertyWebkitUserDrag:
      return 337;
    case CSSPropertyWebkitUserModify:
      return 338;
    case CSSPropertyAliasWebkitUserSelect:
      return 339;
    // case CSSPropertyWebkitFlowInto: return 340;
    // case CSSPropertyWebkitFlowFrom: return 341;
    // case CSSPropertyWebkitRegionFragment: return 342;
    // case CSSPropertyWebkitRegionBreakAfter: return 343;
    // case CSSPropertyWebkitRegionBreakBefore: return 344;
    // case CSSPropertyWebkitRegionBreakInside: return 345;
    // case CSSPropertyShapeInside: return 346;
    case CSSPropertyShapeOutside:
      return 347;
    case CSSPropertyShapeMargin:
      return 348;
    // case CSSPropertyShapePadding: return 349;
    // case CSSPropertyWebkitWrapFlow: return 350;
    // case CSSPropertyWebkitWrapThrough: return 351;
    // CSSPropertyWebkitWrap was 352.
    // 353 was CSSPropertyWebkitTapHighlightColor (duplicated due to #ifdef).
    // 354 was CSSPropertyWebkitAppRegion (duplicated due to #ifdef).
    case CSSPropertyClipPath:
      return 355;
    case CSSPropertyClipRule:
      return 356;
    case CSSPropertyMask:
      return 357;
    // CSSPropertyEnableBackground has been removed, was return 358;
    case CSSPropertyFilter:
      return 359;
    case CSSPropertyFloodColor:
      return 360;
    case CSSPropertyFloodOpacity:
      return 361;
    case CSSPropertyLightingColor:
      return 362;
    case CSSPropertyStopColor:
      return 363;
    case CSSPropertyStopOpacity:
      return 364;
    case CSSPropertyColorInterpolation:
      return 365;
    case CSSPropertyColorInterpolationFilters:
      return 366;
    // case CSSPropertyColorProfile: return 367;
    case CSSPropertyColorRendering:
      return 368;
    case CSSPropertyFill:
      return 369;
    case CSSPropertyFillOpacity:
      return 370;
    case CSSPropertyFillRule:
      return 371;
    case CSSPropertyMarker:
      return 372;
    case CSSPropertyMarkerEnd:
      return 373;
    case CSSPropertyMarkerMid:
      return 374;
    case CSSPropertyMarkerStart:
      return 375;
    case CSSPropertyMaskType:
      return 376;
    case CSSPropertyShapeRendering:
      return 377;
    case CSSPropertyStroke:
      return 378;
    case CSSPropertyStrokeDasharray:
      return 379;
    case CSSPropertyStrokeDashoffset:
      return 380;
    case CSSPropertyStrokeLinecap:
      return 381;
    case CSSPropertyStrokeLinejoin:
      return 382;
    case CSSPropertyStrokeMiterlimit:
      return 383;
    case CSSPropertyStrokeOpacity:
      return 384;
    case CSSPropertyStrokeWidth:
      return 385;
    case CSSPropertyAlignmentBaseline:
      return 386;
    case CSSPropertyBaselineShift:
      return 387;
    case CSSPropertyDominantBaseline:
      return 388;
    // CSSPropertyGlyphOrientationHorizontal has been removed, was return 389;
    // CSSPropertyGlyphOrientationVertical has been removed, was return 390;
    // CSSPropertyKerning has been removed, was return 391;
    case CSSPropertyTextAnchor:
      return 392;
    case CSSPropertyVectorEffect:
      return 393;
    case CSSPropertyWritingMode:
      return 394;
// CSSPropertyWebkitSvgShadow has been removed, was return 395;
// CSSPropertyWebkitCursorVisibility has been removed, was return 396;
// CSSPropertyImageOrientation has been removed, was return 397;
// CSSPropertyImageResolution has been removed, was return 398;
#if defined(ENABLE_CSS_COMPOSITING) && ENABLE_CSS_COMPOSITING
    case CSSPropertyWebkitBlendMode:
      return 399;
    case CSSPropertyWebkitBackgroundBlendMode:
      return 400;
#endif
    case CSSPropertyTextDecorationLine:
      return 401;
    case CSSPropertyTextDecorationStyle:
      return 402;
    case CSSPropertyTextDecorationColor:
      return 403;
    case CSSPropertyTextAlignLast:
      return 404;
    case CSSPropertyTextUnderlinePosition:
      return 405;
    case CSSPropertyMaxZoom:
      return 406;
    case CSSPropertyMinZoom:
      return 407;
    case CSSPropertyOrientation:
      return 408;
    case CSSPropertyUserZoom:
      return 409;
    // CSSPropertyWebkitDashboardRegion was 410.
    // CSSPropertyWebkitOverflowScrolling was 411.
    case CSSPropertyWebkitAppRegion:
      return 412;
    case CSSPropertyAliasWebkitFilter:
      return 413;
    case CSSPropertyWebkitBoxDecorationBreak:
      return 414;
    case CSSPropertyWebkitTapHighlightColor:
      return 415;
    case CSSPropertyBufferedRendering:
      return 416;
    case CSSPropertyGridAutoRows:
      return 417;
    case CSSPropertyGridAutoColumns:
      return 418;
    case CSSPropertyBackgroundBlendMode:
      return 419;
    case CSSPropertyMixBlendMode:
      return 420;
    case CSSPropertyTouchAction:
      return 421;
    case CSSPropertyGridArea:
      return 422;
    case CSSPropertyGridTemplateAreas:
      return 423;
    case CSSPropertyAnimation:
      return 424;
    case CSSPropertyAnimationDelay:
      return 425;
    case CSSPropertyAnimationDirection:
      return 426;
    case CSSPropertyAnimationDuration:
      return 427;
    case CSSPropertyAnimationFillMode:
      return 428;
    case CSSPropertyAnimationIterationCount:
      return 429;
    case CSSPropertyAnimationName:
      return 430;
    case CSSPropertyAnimationPlayState:
      return 431;
    case CSSPropertyAnimationTimingFunction:
      return 432;
    case CSSPropertyObjectFit:
      return 433;
    case CSSPropertyPaintOrder:
      return 434;
    case CSSPropertyMaskSourceType:
      return 435;
    case CSSPropertyIsolation:
      return 436;
    case CSSPropertyObjectPosition:
      return 437;
    // case CSSPropertyInternalCallback: return 438;
    case CSSPropertyShapeImageThreshold:
      return 439;
    case CSSPropertyColumnFill:
      return 440;
    case CSSPropertyTextJustify:
      return 441;
    // CSSPropertyTouchActionDelay was 442
    case CSSPropertyJustifySelf:
      return 443;
    case CSSPropertyScrollBehavior:
      return 444;
    case CSSPropertyWillChange:
      return 445;
    case CSSPropertyTransform:
      return 446;
    case CSSPropertyTransformOrigin:
      return 447;
    case CSSPropertyTransformStyle:
      return 448;
    case CSSPropertyPerspective:
      return 449;
    case CSSPropertyPerspectiveOrigin:
      return 450;
    case CSSPropertyBackfaceVisibility:
      return 451;
    case CSSPropertyGridTemplate:
      return 452;
    case CSSPropertyGrid:
      return 453;
    case CSSPropertyAll:
      return 454;
    case CSSPropertyJustifyItems:
      return 455;
    case CSSPropertyAliasMotionPath:
      return 457;
    case CSSPropertyAliasMotionOffset:
      return 458;
    case CSSPropertyAliasMotionRotation:
      return 459;
    case CSSPropertyMotion:
      return 460;
    case CSSPropertyX:
      return 461;
    case CSSPropertyY:
      return 462;
    case CSSPropertyRx:
      return 463;
    case CSSPropertyRy:
      return 464;
    case CSSPropertyFontSizeAdjust:
      return 465;
    case CSSPropertyCx:
      return 466;
    case CSSPropertyCy:
      return 467;
    case CSSPropertyR:
      return 468;
    case CSSPropertyAliasEpubCaptionSide:
      return 469;
    case CSSPropertyAliasEpubTextCombine:
      return 470;
    case CSSPropertyAliasEpubTextEmphasis:
      return 471;
    case CSSPropertyAliasEpubTextEmphasisColor:
      return 472;
    case CSSPropertyAliasEpubTextEmphasisStyle:
      return 473;
    case CSSPropertyAliasEpubTextOrientation:
      return 474;
    case CSSPropertyAliasEpubTextTransform:
      return 475;
    case CSSPropertyAliasEpubWordBreak:
      return 476;
    case CSSPropertyAliasEpubWritingMode:
      return 477;
    case CSSPropertyAliasWebkitAlignContent:
      return 478;
    case CSSPropertyAliasWebkitAlignItems:
      return 479;
    case CSSPropertyAliasWebkitAlignSelf:
      return 480;
    case CSSPropertyAliasWebkitBorderBottomLeftRadius:
      return 481;
    case CSSPropertyAliasWebkitBorderBottomRightRadius:
      return 482;
    case CSSPropertyAliasWebkitBorderTopLeftRadius:
      return 483;
    case CSSPropertyAliasWebkitBorderTopRightRadius:
      return 484;
    case CSSPropertyAliasWebkitBoxSizing:
      return 485;
    case CSSPropertyAliasWebkitFlex:
      return 486;
    case CSSPropertyAliasWebkitFlexBasis:
      return 487;
    case CSSPropertyAliasWebkitFlexDirection:
      return 488;
    case CSSPropertyAliasWebkitFlexFlow:
      return 489;
    case CSSPropertyAliasWebkitFlexGrow:
      return 490;
    case CSSPropertyAliasWebkitFlexShrink:
      return 491;
    case CSSPropertyAliasWebkitFlexWrap:
      return 492;
    case CSSPropertyAliasWebkitJustifyContent:
      return 493;
    case CSSPropertyAliasWebkitOpacity:
      return 494;
    case CSSPropertyAliasWebkitOrder:
      return 495;
    case CSSPropertyAliasWebkitShapeImageThreshold:
      return 496;
    case CSSPropertyAliasWebkitShapeMargin:
      return 497;
    case CSSPropertyAliasWebkitShapeOutside:
      return 498;
    case CSSPropertyScrollSnapType:
      return 499;
    case CSSPropertyScrollSnapPointsX:
      return 500;
    case CSSPropertyScrollSnapPointsY:
      return 501;
    case CSSPropertyScrollSnapCoordinate:
      return 502;
    case CSSPropertyScrollSnapDestination:
      return 503;
    case CSSPropertyTranslate:
      return 504;
    case CSSPropertyRotate:
      return 505;
    case CSSPropertyScale:
      return 506;
    case CSSPropertyImageOrientation:
      return 507;
    case CSSPropertyBackdropFilter:
      return 508;
    case CSSPropertyTextCombineUpright:
      return 509;
    case CSSPropertyTextOrientation:
      return 510;
    case CSSPropertyGridColumnGap:
      return 511;
    case CSSPropertyGridRowGap:
      return 512;
    case CSSPropertyGridGap:
      return 513;
    case CSSPropertyFontFeatureSettings:
      return 514;
    case CSSPropertyVariable:
      return 515;
    case CSSPropertyFontDisplay:
      return 516;
    case CSSPropertyContain:
      return 517;
    case CSSPropertyD:
      return 518;
    case CSSPropertySnapHeight:
      return 519;
    case CSSPropertyBreakAfter:
      return 520;
    case CSSPropertyBreakBefore:
      return 521;
    case CSSPropertyBreakInside:
      return 522;
    case CSSPropertyColumnCount:
      return 523;
    case CSSPropertyColumnGap:
      return 524;
    case CSSPropertyColumnRule:
      return 525;
    case CSSPropertyColumnRuleColor:
      return 526;
    case CSSPropertyColumnRuleStyle:
      return 527;
    case CSSPropertyColumnRuleWidth:
      return 528;
    case CSSPropertyColumnSpan:
      return 529;
    case CSSPropertyColumnWidth:
      return 530;
    case CSSPropertyColumns:
      return 531;
    case CSSPropertyApplyAtRule:
      return 532;
    case CSSPropertyFontVariantCaps:
      return 533;
    case CSSPropertyHyphens:
      return 534;
    case CSSPropertyFontVariantNumeric:
      return 535;
    case CSSPropertyTextSizeAdjust:
      return 536;
    case CSSPropertyAliasWebkitTextSizeAdjust:
      return 537;
    case CSSPropertyOverflowAnchor:
      return 538;
    case CSSPropertyUserSelect:
      return 539;
    case CSSPropertyOffsetDistance:
      return 540;
    case CSSPropertyOffsetPath:
      return 541;
    case CSSPropertyOffsetRotation:
      return 542;
    case CSSPropertyOffset:
      return 543;
    case CSSPropertyOffsetAnchor:
      return 544;
    case CSSPropertyOffsetPosition:
      return 545;
    case CSSPropertyTextDecorationSkip:
      return 546;
    case CSSPropertyCaretColor:
      return 547;
    case CSSPropertyOffsetRotate:
      return 548;
    case CSSPropertyFontVariationSettings:
      return 549;
    case CSSPropertyInlineSize:
      return 550;
    case CSSPropertyBlockSize:
      return 551;
    case CSSPropertyMinInlineSize:
      return 552;
    case CSSPropertyMinBlockSize:
      return 553;
    case CSSPropertyMaxInlineSize:
      return 554;
    case CSSPropertyMaxBlockSize:
      return 555;
    // 1. Add new features above this line (don't change the assigned numbers of
    // the existing items).
    // 2. Update kMaximumCSSSampleId with the new maximum value.
    // 3. Run the update_use_counter_css.py script in
    // chromium/src/tools/metrics/histograms to update the UMA histogram names.

    case CSSPropertyInvalid:
      ASSERT_NOT_REACHED();
      return 0;
  }

  ASSERT_NOT_REACHED();
  return 0;
}

UseCounter::UseCounter(Context context)
    : m_muteCount(0),
      m_disableReporting(false),
      m_context(context),
      m_featuresRecorded(NumberOfFeatures),
      m_CSSRecorded(lastUnresolvedCSSProperty + 1) {}

void UseCounter::muteForInspector() {
  m_muteCount++;
}

void UseCounter::unmuteForInspector() {
  m_muteCount--;
}

void UseCounter::recordMeasurement(Feature feature) {
  if (m_muteCount)
    return;

  DCHECK(feature != OBSOLETE_PageDestruction &&
         feature !=
             PageVisits);  // PageDestruction is reserved as a scaling factor.
  DCHECK(feature < NumberOfFeatures);

  if (!m_featuresRecorded.quickGet(feature)) {
    // Note that HTTPArchive tooling looks specifically for this event - see
    // https://github.com/HTTPArchive/httparchive/issues/59
    if (!m_disableReporting) {
      TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("blink.feature_usage"),
                   "FeatureFirstUsed", "feature", feature);
      featuresHistogram().count(feature);
    }
    m_featuresRecorded.quickSet(feature);
  }
  m_legacyCounter.countFeature(feature);
}

bool UseCounter::hasRecordedMeasurement(Feature feature) const {
  if (m_muteCount)
    return false;

  DCHECK(feature != OBSOLETE_PageDestruction &&
         feature !=
             PageVisits);  // PageDestruction is reserved as a scaling factor.
  DCHECK(feature < NumberOfFeatures);

  return m_featuresRecorded.quickGet(feature);
}

void UseCounter::didCommitLoad(KURL url) {
  m_legacyCounter.updateMeasurements();

  // Reset state from previous load.
  m_disableReporting = false;

  // Use the protocol of the document being loaded into the main frame to
  // decide whether this page is interesting from a metrics perspective.
  // Note that SVGImage cases always have an about:blank URL
  if (m_context == DefaultContext &&
      !SchemeRegistry::shouldTrackUsageMetricsForScheme(url.protocol())) {
    m_disableReporting = true;
  }

  m_featuresRecorded.clearAll();
  m_CSSRecorded.clearAll();
  if (!m_disableReporting && !m_muteCount) {
    featuresHistogram().count(PageVisits);
    cssHistogram().count(totalPagesMeasuredCSSSampleId());
  }
}

void UseCounter::count(const Frame* frame, Feature feature) {
  if (!frame)
    return;
  FrameHost* host = frame->host();
  if (!host)
    return;

  host->useCounter().count(feature);
}

void UseCounter::count(const Document& document, Feature feature) {
  count(document.frame(), feature);
}

bool UseCounter::isCounted(Document& document, Feature feature) {
  Frame* frame = document.frame();
  if (!frame)
    return false;
  FrameHost* host = frame->host();
  if (!host)
    return false;
  return host->useCounter().hasRecordedMeasurement(feature);
}

bool UseCounter::isCounted(CSSPropertyID unresolvedProperty) {
  return m_CSSRecorded.quickGet(unresolvedProperty);
}

bool UseCounter::isCounted(Document& document, const String& string) {
  Frame* frame = document.frame();
  if (!frame)
    return false;
  FrameHost* host = frame->host();
  if (!host)
    return false;

  CSSPropertyID unresolvedProperty = unresolvedCSSPropertyID(string);
  if (unresolvedProperty == CSSPropertyInvalid)
    return false;
  return host->useCounter().isCounted(unresolvedProperty);
}

void UseCounter::count(ExecutionContext* context, Feature feature) {
  if (!context)
    return;
  if (context->isDocument()) {
    count(*toDocument(context), feature);
    return;
  }
  if (context->isWorkerOrWorkletGlobalScope())
    toWorkerOrWorkletGlobalScope(context)->countFeature(feature);
}

void UseCounter::countCrossOriginIframe(const Document& document,
                                        Feature feature) {
  LocalFrame* frame = document.frame();
  if (frame && frame->isCrossOriginSubframe())
    count(frame, feature);
}

void UseCounter::count(CSSParserMode cssParserMode, CSSPropertyID property) {
  DCHECK(isCSSPropertyIDWithName(property));

  if (!isUseCounterEnabledForMode(cssParserMode) || m_muteCount)
    return;

  if (!m_CSSRecorded.quickGet(property)) {
    // Note that HTTPArchive tooling looks specifically for this event - see
    // https://github.com/HTTPArchive/httparchive/issues/59
    int sampleId = mapCSSPropertyIdToCSSSampleIdForHistogram(property);
    if (!m_disableReporting) {
      TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("blink.feature_usage"),
                   "CSSFirstUsed", "feature", sampleId);
      cssHistogram().count(sampleId);
    }
    m_CSSRecorded.quickSet(property);
  }
  m_legacyCounter.countCSS(property);
}

void UseCounter::count(Feature feature) {
  recordMeasurement(feature);
}

EnumerationHistogram& UseCounter::featuresHistogram() const {
  // Every SVGImage has it's own Page instance, and multiple web pages can
  // share the usage of a single SVGImage.  Ideally perhaps we'd delegate
  // metrics from an SVGImage to one of the Page's it's displayed in, but
  // that's tricky (SVGImage is intentionally isolated, and the Page that
  // created it may not even exist anymore).
  // So instead we just use a dedicated histogram for the SVG case.
  DEFINE_STATIC_LOCAL(
      blink::EnumerationHistogram, histogram,
      ("Blink.UseCounter.Features", blink::UseCounter::NumberOfFeatures));
  DEFINE_STATIC_LOCAL(blink::EnumerationHistogram, svgHistogram,
                      ("Blink.UseCounter.SVGImage.Features",
                       blink::UseCounter::NumberOfFeatures));

  return m_context == SVGImageContext ? svgHistogram : histogram;
}

EnumerationHistogram& UseCounter::cssHistogram() const {
  DEFINE_STATIC_LOCAL(blink::EnumerationHistogram, histogram,
                      ("Blink.UseCounter.CSSProperties", kMaximumCSSSampleId));
  DEFINE_STATIC_LOCAL(
      blink::EnumerationHistogram, svgHistogram,
      ("Blink.UseCounter.SVGImage.CSSProperties", kMaximumCSSSampleId));

  return m_context == SVGImageContext ? svgHistogram : histogram;
}

/*
 *
 * LEGACY metrics support - WebCore.FeatureObserver is to be superceded by
 * WebCore.UseCounter
 *
 */

static EnumerationHistogram& featureObserverHistogram() {
  DEFINE_STATIC_LOCAL(
      EnumerationHistogram, histogram,
      ("WebCore.FeatureObserver", UseCounter::NumberOfFeatures));
  return histogram;
}

UseCounter::LegacyCounter::LegacyCounter()
    : m_featureBits(NumberOfFeatures),
      m_CSSBits(lastUnresolvedCSSProperty + 1) {}

UseCounter::LegacyCounter::~LegacyCounter() {
  // PageDestruction was intended to be used as a scale, but it's broken (due to
  // fast shutdown).  See https://crbug.com/597963.
  featureObserverHistogram().count(OBSOLETE_PageDestruction);
  updateMeasurements();
}

void UseCounter::LegacyCounter::countFeature(Feature feature) {
  m_featureBits.quickSet(feature);
}

void UseCounter::LegacyCounter::countCSS(CSSPropertyID property) {
  m_CSSBits.quickSet(property);
}

void UseCounter::LegacyCounter::updateMeasurements() {
  EnumerationHistogram& featureHistogram = featureObserverHistogram();
  featureHistogram.count(PageVisits);
  for (size_t i = 0; i < NumberOfFeatures; ++i) {
    if (m_featureBits.quickGet(i))
      featureHistogram.count(i);
  }
  // Clearing count bits is timing sensitive.
  m_featureBits.clearAll();

  // FIXME: Sometimes this function is called more than once per page. The
  // following bool guards against incrementing the page count when there are no
  // CSS bits set. https://crbug.com/236262.
  DEFINE_STATIC_LOCAL(
      EnumerationHistogram, cssPropertiesHistogram,
      ("WebCore.FeatureObserver.CSSProperties", kMaximumCSSSampleId));
  bool needsPagesMeasuredUpdate = false;
  for (size_t i = firstCSSProperty; i <= lastUnresolvedCSSProperty; ++i) {
    if (m_CSSBits.quickGet(i)) {
      int cssSampleId = mapCSSPropertyIdToCSSSampleIdForHistogram(
          static_cast<CSSPropertyID>(i));
      cssPropertiesHistogram.count(cssSampleId);
      needsPagesMeasuredUpdate = true;
    }
  }

  if (needsPagesMeasuredUpdate)
    cssPropertiesHistogram.count(totalPagesMeasuredCSSSampleId());

  m_CSSBits.clearAll();
}

}  // namespace blink
