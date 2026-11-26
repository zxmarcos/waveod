// The wires of the pedal's 2 x 6 grid (src/model/route.h) as SVG paths, for any size of drawing: the chain
// screen's stage and the presets screen's miniature. Series runs row 1 then row 2; parallel splits row 1 at
// `split` into lane A (up to `merge`) and lane B (the whole row 2); dual L/R sends lane A to the left output and
// row 2 to the right one.

export interface GridGeometry {
  x0: number;       // left edge of column 0
  pitch: number;    // column width
  y1: number;       // row 1's centre
  y2: number;       // row 2's centre
  bus: number;      // where the wires cross between the rows
  inEnd: number;    // where the IN pill ends
  outX: number;     // where the OUT pill starts
  r: number;        // corner radius
  pad: number;      // how far the U-turns stand off the grid
}

export interface RouteShape { mode: number; split: number; merge: number }

/** the paths, and the heights of the outputs (two for dual L/R) */
export function wirePaths(g: GridGeometry, route: RouteShape, row2Used: boolean) {
  const { x0, pitch, y1, y2, bus, inEnd, outX, r, pad } = g;
  const xend = x0 + 6 * pitch;
  const xs = x0 + route.split * pitch;
  const xm = x0 + (route.mode === 2 ? 6 : route.merge) * pitch;
  if (route.mode === 0) {
    if (!row2Used) return { main: [`M${inEnd} ${y1}H${outX}`], outs: [y1] };
    return {
      main: [
        `M${inEnd} ${y1}H${xend + pad - r}a${r} ${r} 0 0 1 ${r} ${r}V${bus - r}a${r} ${r} 0 0 1 ${-r} ${r}` +
        `H${x0 - pad + r}a${r} ${r} 0 0 0 ${-r} ${r}V${y2 - r}a${r} ${r} 0 0 0 ${r} ${r}H${outX}`,
      ],
      outs: [y2],
    };
  }
  const laneB =
    `M${xs} ${y1}V${bus - r}a${r} ${r} 0 0 1 ${-r} ${r}H${x0 - pad + r}a${r} ${r} 0 0 0 ${-r} ${r}` +
    `V${y2 - r}a${r} ${r} 0 0 0 ${r} ${r}`;
  if (route.mode === 2) return { main: [`M${inEnd} ${y1}H${outX}`, laneB + `H${outX}`], outs: [y1, y2] };
  return {
    main: [
      `M${inEnd} ${y1}H${outX}`,
      laneB + `H${xend + pad - r}a${r} ${r} 0 0 0 ${r} ${-r}V${bus + r}a${r} ${r} 0 0 0 ${-r} ${-r}H${xm + r}` +
        `a${r} ${r} 0 0 1 ${-r} ${-r}V${y1}`,
    ],
    outs: [y1],
  };
}
