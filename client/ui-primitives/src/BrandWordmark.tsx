import type { IconProps } from './icons/props.ts'
import { BRAND_MARK_DATA_URI } from './BrandMark.tsx'

/** Display options for the 伯乐智力工厂 wordmark. */
export interface BrandWordmarkProps extends IconProps {
  /** Whether to include the leading square mark; defaults to true. */
  includeMark?: boolean | undefined
}

/**
 * The 伯乐智力工厂 outline glyphs, verbatim from the official artwork
 * (`logo/720x129 3.svg`, path id `伯乐智力工厂`). The source art is drawn on a
 * wider canvas, hence the `translate(-295)` folded into
 * {@link BRAND_WORDMARK_TRANSFORM}.
 */
export const BRAND_WORDMARK_PATH =
  'M475.524,79.967V74.9h20.531v5.063h10.406V22.311h-18l2.813-8.437L479.18,13.03a69.813,69.813,0,0,1-1.969,9.281H465.118V79.967h10.406Zm-31.5-24.75q2.808-3.935,4.219-5.906V80.249h9.843v-49.5a154.373,154.373,0,0,0,5.907-15.469l-10.688-2.531a109.668,109.668,0,0,1-12.937,30.938q1.4,6.75,2.531,12.938a5.955,5.955,0,0,1,1.125-1.406h0Zm52.031-1.969V66.186H475.524V53.249h20.531Zm0-21.937V44.249H475.524V31.311h20.531Zm46.969,19.406V65.061q0.276,5.063-4.219,4.781-5.062,0-8.719-.281,0.844,3.375,2.25,10.125,8.154,0,10.969-.281,11.529,0,10.688-11.531V50.717H577.9V41.155H553.993V29.342H543.024V41.155H528.4L529.8,26.811q26.433-1.121,45.563-2.531l-3.375-9.844q-24.47,2.254-52.032,2.813L516.3,50.717h26.719Zm-6.75,6.75-9-5.625q-9,12.94-14.906,18.844a64.128,64.128,0,0,1,8.437,6.469q5.624-5.906,15.469-19.687h0Zm44.719,13.5q-7.034-8.437-15.469-18.281l-8.156,5.344q5.622,6.75,14.625,18.844Zm17.156-34.594q-2.532,3.942-13.219,7.594a13.3,13.3,0,0,0,1.688,1.969,43.1,43.1,0,0,1,4.218,5.625A0.9,0.9,0,0,0,591.68,51v29.25H601.8V77.436h34.313v2.813h10.125V47.9h-29.25l3.937-5.344q-6.189-3.652-10.969-6.187h11.25V29.061h-11.25a32.456,32.456,0,0,0,.282-4.5h9.281V17.249H600.4a9.159,9.159,0,0,0,1.687-3.375,2.192,2.192,0,0,0,.282-0.844l-9.563-.281A22.141,22.141,0,0,1,584.93,24.28q2.249,2.255,4.781,4.781h-4.218v7.313h12.656ZM650.18,44.53V16.967h-27V44.53h27ZM636.118,66.186V70.4H601.8V66.186h34.313Zm0-10.406v3.938H601.8V55.78h34.313Zm5.062-30.656V36.655h-9V25.124h9ZM597.586,47.9a20.169,20.169,0,0,0,7.032-5.625,39.154,39.154,0,0,1,4.5,2.813,41.439,41.439,0,0,1,4.218,2.813h-15.75Zm3.094-23.344v4.5h-8.437a37.392,37.392,0,0,0,3.937-4.5h4.5Zm89.156,12.094h19.969c0,0.941-.1,2.439-0.281,4.5q-0.283,13.5-.844,19.125-0.283,8.438-7.594,7.875-9.848,0-14.062-.281,1.4,5.629,2.25,10.688,9.558,0,16.031-.281,14.061,0,14.344-13.219,0.843-14.621,1.969-38.812H690.68l0.844-13.219H679.993L679.43,26.249H658.9V36.655h19.687Q678.863,60,656.649,71.811l7.594,8.719q25.588-14.06,25.593-43.875h0ZM756.774,28.5V65.9h-27v9.844h66.094V65.9H768.586V28.5h23.625V18.092H733.43V28.5h23.344Zm61.594,34.594q1.963-7.87,1.968-22.781V27.374H868.43V16.4H808.805V40.03q0,12.94-1.687,19.125a32.316,32.316,0,0,1-6.469,12.938q1.12,0.844,3.375,2.813,5.063,4.219,6.187,5.344a49.3,49.3,0,0,0,8.157-17.156h0Z'

/**
 * Geometry that lands the outline ink on this component's 24-unit-tall
 * artboard: 14 units of ink height starting at y 4.62, with the name ink
 * beginning at x 32 so the mark keeps the x 0..24 slot.
 */
export const BRAND_WORDMARK_TRANSFORM =
  'translate(1.9852,1.9853) scale(0.206642) translate(-295)'

/** Artboard metrics for {@link BrandWordmark} (user units). */
export const BRAND_WORDMARK_VIEWBOX = {
  height: 24,
  /** viewBox width with the leading mark. */
  width: 126,
  /** viewBox width of the name artwork alone. */
  nameWidth: 94,
  /** Left edge the name-only viewBox starts at. */
  nameX: 32,
}

/**
 * The `Mentor Homin` outline glyphs, verbatim from the same official artwork
 * (`720x129 3.svg`, path id `Mentor_Homin`): the Latin name line below
 * {@link BRAND_WORDMARK_PATH}.
 */
export const BRAND_LOCKUP_TAGLINE_PATH =
  'M553.033,117.607V101.983q0-1.908,1.044-1.908a0.728,0.728,0,0,1,.756.45,4.266,4.266,0,0,1,.18,1.458v15.624h7.128V101.875q0-1.8,1.08-1.8a0.7,0.7,0,0,1,.72.432,4.238,4.238,0,0,1,.18,1.476v15.624h7.164V100.939a8.857,8.857,0,0,0-1.728-5.886,6.159,6.159,0,0,0-4.968-2.034,7,7,0,0,0-3.51.81,7.169,7.169,0,0,0-2.5,2.574,6.249,6.249,0,0,0-5.688-3.384,6.718,6.718,0,0,0-5.058,2.034,7.232,7.232,0,0,0-1.962,5.238v17.316h7.164Zm39.384-8.964a8.616,8.616,0,0,0-2.61-6.39,8.757,8.757,0,0,0-6.39-2.574,9.024,9.024,0,0,0-6.624,2.646,9.279,9.279,0,0,0,.018,13.032A8.954,8.954,0,0,0,583.345,118a9.155,9.155,0,0,0,5.832-2.052l-3.744-4.536a2.573,2.573,0,0,1-1.908,1.008,2.274,2.274,0,0,1-1.944-1.1,4.81,4.81,0,0,1-.756-2.79,4.35,4.35,0,0,1,.792-2.7,2.4,2.4,0,0,1,1.98-1.044,2.057,2.057,0,0,1,2.2,1.98h-3.348a4.716,4.716,0,0,0-.324,1.656,4.33,4.33,0,0,0,.252,1.44h9.972q0.072-1.08.072-1.224h0Zm18.252-.324a9.026,9.026,0,0,0-2.106-6.354,7.973,7.973,0,0,0-11.25.072,8.019,8.019,0,0,0-2.268,5.778v9.792h6.588v-9.936q0-1.512,1.224-1.512,1.26,0,1.26,1.512v9.936h6.552v-9.288ZM613.8,93.6v14.544q0,4.718,2.556,7.29A9.547,9.547,0,0,0,623.449,118v-6.912a3,3,0,0,1-2.412-.792,4.7,4.7,0,0,1-.648-2.88h2.952v-6.948h-2.952V93.6H613.8ZM628.237,115.3a9.04,9.04,0,1,0-2.7-6.444A8.838,8.838,0,0,0,628.237,115.3Zm4.59-8.334a2.664,2.664,0,0,1,4.554,1.89,2.529,2.529,0,0,1-.774,1.854,2.695,2.695,0,0,1-3.78,0,2.529,2.529,0,0,1-.774-1.854A2.574,2.574,0,0,1,632.827,106.969Zm21.366-7.29a7.037,7.037,0,0,0-5.49,2.25,8.759,8.759,0,0,0-2.034,6.1v9.576h6.588v-8.316a3.89,3.89,0,0,1,.45-2.052,1.4,1.4,0,0,1,1.242-.72,2.626,2.626,0,0,1,1.332.432V99.9a10.641,10.641,0,0,0-2.088-.216h0Zm20.448,2.52V93.6h-7.128v24.012h7.128v-9.072h1.98V102.2h-1.98Zm3.2-8.6v24.012h7.128V93.6h-7.128Zm13.14,21.708a9.04,9.04,0,1,0-2.7-6.444A8.838,8.838,0,0,0,690.985,115.3Zm4.59-8.334a2.664,2.664,0,0,1,4.554,1.89,2.529,2.529,0,0,1-.774,1.854,2.695,2.695,0,0,1-3.78,0,2.529,2.529,0,0,1-.774-1.854A2.574,2.574,0,0,1,695.575,106.969Zm37.89,0.558a8.4,8.4,0,0,0-1.836-5.8,6.375,6.375,0,0,0-5-2.052,7.567,7.567,0,0,0-5.148,2.484,7.156,7.156,0,0,0-5.256-2.484,6.423,6.423,0,0,0-4.824,2.07,7,7,0,0,0-1.98,5.022v10.836h6.588v-9.468q0-1.944,1.08-1.944a0.921,0.921,0,0,1,.828.378,3.088,3.088,0,0,1,.252,1.53v9.5h6.552v-9.468q0-1.944,1.116-1.944,1.044,0,1.044,1.908v9.5h6.588v-10.08Zm4.95-9.846a3.216,3.216,0,1,0-.918-2.25A3.06,3.06,0,0,0,738.415,97.681Zm-1.1,2.394v17.532h6.588V100.075h-6.588Zm26.064,8.244a9.026,9.026,0,0,0-2.106-6.354,7.973,7.973,0,0,0-11.25.072,8.019,8.019,0,0,0-2.268,5.778v9.792h6.588v-9.936q0-1.512,1.224-1.512,1.26,0,1.26,1.512v9.936h6.552v-9.288Z'

/** Artboard metrics for {@link BrandLockup} (user units): a viewBox window
 * onto the official 720x129 source that frames the name and tagline ink
 * tightly, so the lockup renders as one clean piece with no dead canvas and
 * the tagline reads as a second text line instead of a thin sliver. */
export const BRAND_LOCKUP_VIEWBOX = { x: 137, y: 5, width: 448, height: 119 }

/**
 * Render the complete brand lockup: the name line with the Latin name line
 * beneath it. The hero shows this instead of a separate mark and headline text.
 * @param props.size - height in px (default 34; width follows the artwork).
 * @param props.className - extra class for layout placement.
 * @returns the lockup svg (aria-hidden decorative brand art).
 */
export function BrandLockup({ size = 34, className }: IconProps) {
  const { x, y, width, height } = BRAND_LOCKUP_VIEWBOX
  return (
    <svg
      width={(size * width) / height}
      height={size}
      className={className}
      viewBox={`${x} ${y} ${width} ${height}`}
      fill="none"
      aria-hidden="true"
    >
      <g transform="translate(-295)">
        <path d={BRAND_WORDMARK_PATH} fill="currentColor" fillRule="evenodd" />
        <path d={BRAND_LOCKUP_TAGLINE_PATH} fill="currentColor" fillRule="evenodd" />
      </g>
    </svg>
  )
}

/**
 * Render the full brand wordmark.
 * @param props.size - height in px (default 24; width follows the artwork).
 * @param props.className - extra class for layout placement.
 * @param props.includeMark - whether to include the leading square mark.
 * @returns the wordmark svg (aria-hidden decorative brand art).
 */
export function BrandWordmark({ size = 24, className, includeMark = true }: BrandWordmarkProps) {
  const { height, width, nameWidth, nameX } = BRAND_WORDMARK_VIEWBOX
  const box = includeMark ? `0 0 ${width} ${height}` : `${nameX} 0 ${nameWidth} ${height}`
  return (
    <svg
      width={(size * (includeMark ? width : nameWidth)) / height}
      height={size}
      className={className}
      viewBox={box}
      fill="none"
      aria-hidden="true"
    >
      {includeMark && <image x="0" y="0" width={height} height={height} href={BRAND_MARK_DATA_URI} />}
      <g transform={BRAND_WORDMARK_TRANSFORM}>
        <path d={BRAND_WORDMARK_PATH} fill="currentColor" fillRule="evenodd" />
      </g>
    </svg>
  )
}
