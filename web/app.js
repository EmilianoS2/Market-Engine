const byId = (id) => document.getElementById(id);
const MAX_UINT64 = 18446744073709551615n;

function positiveInteger(value, label) {
  const text = value.trim();
  if (!/^[0-9]+$/.test(text)) {
    throw new Error(`${label} must be a positive whole number.`);
  }
  const number = BigInt(text);
  if (number < 1n || number > MAX_UINT64) {
    throw new Error(`${label} must be between 1 and ${MAX_UINT64}.`);
  }
  return number.toString();
}

async function api(path, options = {}) {
  const response = await fetch(path, options);
  const data = await response.json();
  if (!response.ok || !data.ok) {
    throw new Error(data.error || "The server could not complete the request.");
  }
  return data;
}

function post(path, body) {
  return api(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
}

function setMessage(text, kind = "") {
  const element = byId("message");
  element.textContent = text;
  element.className = `message ${kind}`.trim();
}

function addRow(parent, values, empty = false) {
  const row = document.createElement("tr");
  if (empty) {
    row.className = "empty-row";
    const cell = document.createElement("td");
    cell.colSpan = values.length;
    cell.textContent = values[0];
    row.appendChild(cell);
  } else {
    for (const value of values) {
      const cell = document.createElement("td");
      cell.textContent = value;
      row.appendChild(cell);
    }
  }
  parent.appendChild(row);
}

function renderLevels(bodyId, levels) {
  const body = byId(bodyId);
  body.replaceChildren();
  if (levels.length === 0) {
    addRow(body, ["No resting orders", "", "", ""], true);
    return;
  }
  for (const level of levels) {
    addRow(body, [level.price, level.quantity, level.orderCount, level.marketMakerQuantity ?? "0"]);
  }
}

function renderTrades(trades) {
  byId("trades-count").textContent = `${trades.length} trade${trades.length === 1 ? "" : "s"}`;
  const body = byId("trades-body");
  body.replaceChildren();
  if (trades.length === 0) {
    addRow(body, ["No trades yet", "", "", "", "", ""], true);
    return;
  }
  for (const trade of trades) {
    addRow(body, [trade.makerId, trade.takerId, trade.price, trade.quantity, trade.buyer ?? "—", trade.seller ?? "—"]);
  }
}

const candleView = { candles: [], maker: null };

function renderCandles(candles, maker = null) {
  candleView.candles = candles;
  candleView.maker = maker;
  renderCandleChart(candles, maker);
  const body = byId("candles-body");
  body.replaceChildren();
  if (candles.length === 0) {
    addRow(body, ["No candles yet", "", "", "", "", ""], true);
    return;
  }
  for (const candle of candles) {
    const time = new Date(Number(candle.startTimeMs)).toLocaleString();
    addRow(body, [time, candle.open, candle.high, candle.low, candle.close, candle.volume]);
  }
}

const SVG_NS = "http://www.w3.org/2000/svg";

function svgElement(name, attributes, text) {
  const element = document.createElementNS(SVG_NS, name);
  for (const [key, value] of Object.entries(attributes)) {
    element.setAttribute(key, String(value));
  }
  if (text !== undefined) element.textContent = text;
  return element;
}

function depthSeries(levels) {
  let cumulative = 0n;
  return levels.map((level) => {
    cumulative += BigInt(level.quantity);
    return { price: BigInt(level.price), levelQuantity: level.quantity, cumulative };
  });
}

function axisQuantity(value) {
  for (const [size, suffix] of [[1000000000000n, "T"], [1000000000n, "B"], [1000000n, "M"], [1000n, "K"]]) {
    if (value >= size) {
      const fraction = ((value % size) * 100n / size).toString().padStart(2, "0").replace(/0+$/, "");
      return `${value / size}${fraction ? `.${fraction}` : ""}${suffix}`;
    }
  }
  return value.toString();
}

function renderCandleChart(candles, maker = null) {
  const chart = byId("candle-chart");
  const empty = byId("candle-empty");
  const plot = byId("candle-plot");
  const hover = byId("candle-hover");
  const readout = byId("candle-readout");
  const quotePlot = byId("candle-maker-quotes");
  plot.replaceChildren();
  hover.replaceChildren();
  quotePlot.replaceChildren();
  const showLimits = byId("maker-lines-toggle").checked;
  const quotes = showLimits ? ["bid", "ask"].flatMap((side) => {
    const quote = maker?.[side];
    return quote && BigInt(quote.price ?? 0) > 0n && BigInt(quote.quantity ?? 0) > 0n
      ? [{ ...quote, side, priceValue: BigInt(quote.price) }] : [];
  }) : [];
  const quoteStatus = byId("maker-lines-status");
  quoteStatus.hidden = !showLimits;
  quoteStatus.textContent = quotes.length
    ? "Green = buy limit; red = sell limit. Lines show current resting orders, with price in ticks and shares remaining. They update as orders fill or are replaced."
    : maker?.state === "Running"
      ? "The maker is running with no eligible resting limits."
      : maker?.state === "Faulted"
        ? "The maker has no resting limits. Check its fault in the maker panel."
        : "The maker has no resting limits. Start the maker to see eligible buy and sell quotes.";

  if (candles.length === 0 && quotes.length === 0) {
    chart.setAttribute("hidden", "");
    empty.hidden = false;
    readout.textContent = "Waiting for the first trade";
    return;
  }

  chart.removeAttribute("hidden");
  empty.hidden = true;
  const visible = candles.slice(-48);
  const left = 38;
  const right = quotes.length ? 718 : 846; // Reserve room for live order labels.
  const priceTop = 24;
  const priceBottom = 300;
  const volumeTop = 337;
  const volumeBottom = 412;
  const timeLabelY = 442;
  // Keep a short history compact instead of stretching its candles across the plot.
  const slotWidth = Math.min(28, (right - left) / Math.max(visible.length, 1));
  const startX = left + ((right - left) - visible.length * slotWidth) / 2;
  const x = (index) => startX + (index + 0.5) * slotWidth;
  const bodyWidth = slotWidth * 0.72;

  // Include actual resting prices so quotes outside the candle range stay visible.
  const prices = [...visible.flatMap((candle) => [BigInt(candle.low), BigInt(candle.high)]), ...quotes.map((quote) => quote.priceValue)];
  const lowest = prices.reduce((low, price) => price < low ? price : low);
  const highest = prices.reduce((high, price) => price > high ? price : high);
  const span = highest - lowest;
  const padding = (span === 0n ? highest / 25n : span / 10n) + 1n;
  const domainMin = lowest > padding ? lowest - padding : 0n;
  const domainMax = highest + padding;
  const domainRange = domainMax - domainMin;
  const priceHeight = BigInt(priceBottom - priceTop);
  const yPrice = (price) => priceBottom - Number((price - domainMin) * priceHeight / domainRange);
  const maxVolume = visible.reduce((max, candle) => {
    const volume = BigInt(candle.volume);
    return volume > max ? volume : max;
  }, 0n);
  const volumeHeight = BigInt(volumeBottom - volumeTop);

  for (let tick = 0; tick <= 4; tick += 1) {
    const price = domainMin + domainRange * BigInt(tick) / 4n;
    const y = yPrice(price);
    plot.appendChild(svgElement("line", { x1: left, y1: y, x2: right, y2: y, class: "candle-grid" }));
    plot.appendChild(svgElement("text", { x: right + 12, y: y + 4, class: "candle-axis-label" }, axisQuantity(price)));
  }
  plot.appendChild(svgElement("line", { x1: left, y1: volumeTop - 15, x2: right, y2: volumeTop - 15, class: "candle-divider" }));
  plot.appendChild(svgElement("text", { x: left + 2, y: volumeTop - 1, class: "candle-axis-label" }, "VOLUME"));
  plot.appendChild(svgElement("text", { x: right + 12, y: volumeTop + 8, class: "candle-axis-label" }, axisQuantity(maxVolume)));

  const last = visible.at(-1);
  if (last) {
    const lastCloseY = yPrice(BigInt(last.close));
    plot.appendChild(svgElement("line", { x1: left, y1: lastCloseY, x2: right, y2: lastCloseY, class: "candle-last-price" }));
  } else readout.textContent = "Resting maker limits · waiting for the first trade";

  visible.forEach((candle, index) => {
    const open = BigInt(candle.open);
    const high = BigInt(candle.high);
    const low = BigInt(candle.low);
    const close = BigInt(candle.close);
    const rising = close >= open;
    const direction = rising ? "up" : "down";
    const center = x(index);
    const openY = yPrice(open);
    const closeY = yPrice(close);
    const bodyTop = Math.min(openY, closeY);
    const bodyHeight = Math.max(2, Math.abs(openY - closeY));
    const barHeight = Math.max(1, Number(BigInt(candle.volume) * volumeHeight / (maxVolume || 1n)));

    plot.appendChild(svgElement("line", { x1: center, y1: yPrice(high), x2: center, y2: yPrice(low), class: `candle-wick-${direction}` }));
    plot.appendChild(svgElement("rect", { x: center - bodyWidth / 2, y: bodyTop, width: bodyWidth, height: bodyHeight, rx: 1, class: `candle-body-${direction}` }));
    plot.appendChild(svgElement("rect", { x: center - bodyWidth / 2, y: volumeBottom - barHeight, width: bodyWidth, height: barHeight, class: `candle-volume-${direction}` }));
  });

  // Label the latest candle, then leave room between earlier time labels.
  const labelStep = Math.max(1, Math.ceil(96 / slotWidth));
  for (let index = visible.length - 1; index >= 0; index -= labelStep) {
    const time = new Date(Number(visible[index].startTimeMs))
      .toLocaleTimeString(undefined, { hour: "numeric", minute: "2-digit" });
    plot.appendChild(svgElement("text", { x: x(index), y: timeLabelY, "text-anchor": "middle", class: "candle-axis-label" }, time));
  }

  function showCandle(index) {
    const candle = visible[index];
    const center = x(index);
    const closeY = yPrice(BigInt(candle.close));
    hover.replaceChildren(
      svgElement("line", { x1: center, y1: priceTop, x2: center, y2: volumeBottom, class: "candle-crosshair" }),
      svgElement("line", { x1: left, y1: closeY, x2: right, y2: closeY, class: "candle-crosshair" }),
    );
    const time = new Date(Number(candle.startTimeMs)).toLocaleString();
    readout.textContent = `${time}  ·  O ${candle.open}  H ${candle.high}  L ${candle.low}  C ${candle.close}  V ${candle.volume}`;
  }

  if (visible.length) showCandle(visible.length - 1);
  visible.forEach((candle, index) => {
    const hit = svgElement("rect", {
      x: x(index) - slotWidth / 2,
      y: priceTop,
      width: slotWidth,
      height: volumeBottom - priceTop,
      class: "candle-hit",
      tabindex: 0,
    });
    hit.appendChild(svgElement("title", {}, `${new Date(Number(candle.startTimeMs)).toLocaleString()}: open ${candle.open}, high ${candle.high}, low ${candle.low}, close ${candle.close}, volume ${candle.volume}`));
    hit.addEventListener("mouseenter", () => showCandle(index));
    hit.addEventListener("click", () => showCandle(index));
    hit.addEventListener("focus", () => showCandle(index));
    hit.addEventListener("mouseleave", () => showCandle(visible.length - 1));
    hit.addEventListener("blur", () => showCandle(visible.length - 1));
    plot.appendChild(hit);
  });

  // Keep quote labels distinct even when both prices map to nearby pixels.
  const labelHeight = 38;
  const positioned = quotes.map((quote) => ({ ...quote, y: yPrice(quote.priceValue) }))
    .sort((a, b) => a.y - b.y);
  let previousBottom = priceTop - 4;
  for (const quote of positioned) {
    quote.labelTop = Math.max(previousBottom + 4, Math.max(priceTop, Math.min(quote.y - labelHeight / 2, priceBottom - labelHeight)));
    previousBottom = quote.labelTop + labelHeight;
  }
  const overflow = Math.max(0, previousBottom - priceBottom);
  for (const quote of positioned) {
    quote.labelTop -= overflow;
    const group = svgElement("g", { class: `candle-maker-quote candle-maker-${quote.side}`, "data-order-id": quote.id, "data-side": quote.side });
    const side = quote.side === "bid" ? "BUY" : "SELL";
    group.appendChild(svgElement("title", {}, `Maker ${side.toLowerCase()} order ${quote.id}: ${quote.price} ticks, ${quote.quantity} shares remaining.`));
    group.appendChild(svgElement("line", { x1: left, x2: right, y1: quote.y, y2: quote.y, class: "candle-maker-line" }));
    group.appendChild(svgElement("line", { x1: right, x2: right + 12, y1: quote.y, y2: quote.labelTop + labelHeight / 2, class: "candle-maker-leader" }));
    group.appendChild(svgElement("rect", { x: right + 12, y: quote.labelTop, width: 212, height: labelHeight, rx: 5, class: "candle-maker-label" }));
    group.appendChild(svgElement("text", { x: right + 20, y: quote.labelTop + 15, class: "candle-maker-price" }, `${side} · ${quote.priceValue.toLocaleString()} ticks`));
    group.appendChild(svgElement("text", { x: right + 20, y: quote.labelTop + 30, class: "candle-maker-quantity" }, `${quote.quantity} shares remaining`));
    quotePlot.appendChild(group);
  }
}

byId("maker-lines-toggle").addEventListener("change", () => renderCandleChart(candleView.candles, candleView.maker));

function renderDepth(state) {
  const bids = depthSeries(state.bids);
  const asks = depthSeries(state.asks);
  const chart = byId("depth-chart");
  const empty = byId("depth-empty");
  const plot = byId("depth-plot");
  plot.replaceChildren();

  if (bids.length === 0 && asks.length === 0) {
    chart.setAttribute("hidden", "");
    empty.hidden = false;
    return;
  }
  chart.removeAttribute("hidden");
  empty.hidden = true;

  const left = 66;
  const right = 938;
  const top = 22;
  const bottom = 284;
  const width = BigInt(right - left);
  const height = BigInt(bottom - top);
  const allPrices = [...bids, ...asks].map((point) => point.price);
  const minPrice = allPrices.reduce((a, b) => a < b ? a : b);
  const maxPrice = allPrices.reduce((a, b) => a > b ? a : b);
  const priceRange = maxPrice - minPrice;
  const padding = (priceRange === 0n ? maxPrice / 20n : priceRange / 12n) + 1n;
  const domainMin = minPrice > padding ? minPrice - padding : 0n;
  const domainMax = maxPrice + padding;
  const domainRange = domainMax - domainMin;
  const maxDepth = [bids.at(-1)?.cumulative ?? 0n, asks.at(-1)?.cumulative ?? 0n]
    .reduce((a, b) => a > b ? a : b);
  const x = (price) => left + Number((price - domainMin) * width / domainRange);
  const y = (quantity) => bottom - Number(quantity * height / maxDepth);

  const tickCount = maxDepth < 4n ? Number(maxDepth) : 4;
  for (let i = 0; i <= tickCount; i += 1) {
    const quantity = maxDepth * BigInt(i) / BigInt(tickCount);
    const tickY = y(quantity);
    plot.appendChild(svgElement("line", { x1: left, y1: tickY, x2: right, y2: tickY, class: "depth-grid" }));
    plot.appendChild(svgElement("text", { x: left - 12, y: tickY + 4, "text-anchor": "end", class: "depth-tick" }, axisQuantity(quantity)));
  }

  function drawSide(points, side) {
    if (points.length === 0) return;
    let path = `M ${x(points[0].price)} ${bottom}`;
    let previousQuantity = 0n;
    for (const point of points) {
      const pointX = x(point.price);
      if (previousQuantity > 0n) path += ` L ${pointX} ${y(previousQuantity)}`;
      path += ` L ${pointX} ${y(point.cumulative)}`;
      previousQuantity = point.cumulative;
    }
    const edge = side === "bid" ? left : right;
    path += ` L ${edge} ${y(previousQuantity)}`;
    plot.appendChild(svgElement("path", { d: `${path} L ${edge} ${bottom} Z`, class: `depth-area-${side}` }));
    plot.appendChild(svgElement("path", { d: path, class: `depth-line-${side}` }));
    for (const point of points) {
      const circle = svgElement("circle", { cx: x(point.price), cy: y(point.cumulative), r: 5, class: `depth-point-${side}` });
      circle.appendChild(svgElement("title", {}, `${side === "bid" ? "Bid" : "Ask"} price ${point.price}: ${point.levelQuantity} at this price, ${point.cumulative} cumulative`));
      plot.appendChild(circle);
    }
  }

  drawSide(bids, "bid");
  drawSide(asks, "ask");
  plot.appendChild(svgElement("line", { x1: left, y1: bottom, x2: right, y2: bottom, class: "depth-axis" }));
  for (const price of minPrice === maxPrice ? [minPrice] : [minPrice, maxPrice]) {
    const tickX = x(price);
    plot.appendChild(svgElement("line", { x1: tickX, y1: bottom, x2: tickX, y2: bottom + 6, class: "depth-axis" }));
    plot.appendChild(svgElement("text", { x: tickX, y: bottom + 24, "text-anchor": "middle", class: "depth-tick" }, price.toString()));
  }
}

function render(state) {
  const supportsMarket = state.marketOrdersSupported === true;
  byId("order-market-option").disabled = !supportsMarket;
  byId("order-market-option").textContent = supportsMarket ? "Market" : "Market (restart server to enable)";
  if (!supportsMarket && byId("order-type").value === "market") {
    byId("order-type").value = "limit";
    syncOrderType();
  }
  renderMaker(state.marketMaker);
  renderLevels("bids-body", state.bids);
  renderLevels("asks-body", state.asks);
  renderDepth(state);
  renderTrades(state.trades);
  renderCandles(state.candles ?? [], state.marketMaker ?? null);
  byId("best-bid").textContent = state.bids[0]?.price ?? "—";
  byId("best-ask").textContent = state.asks[0]?.price ?? "—";
  byId("last-price").textContent = state.trades[0]?.price ?? "—";
  byId("spread").textContent = state.bids.length && state.asks.length
    ? (BigInt(state.asks[0].price) - BigInt(state.bids[0].price)).toString()
    : "—";
}

async function refresh() {
  const state = await api("/api/state");
  render(state);
  const connection = byId("connection");
  connection.textContent = "Connected to C++ engine";
  connection.className = "connection online";
  return state;
}

function showError(error) {
  setMessage(error.message, "error");
  if (error instanceof TypeError) {
    const connection = byId("connection");
    connection.textContent = "Server unavailable";
    connection.className = "connection offline";
  }
}

const makerUI = { data: null, initialized: false, busy: false, resetting: false, polling: false, pending: null, timer: null };
const makerFields = {
  referencePrice: "maker-reference", spreadBps: "maker-spread", quoteSize: "maker-size",
  refreshIntervalMs: "maker-interval", skewBpsPerShare: "maker-skew", minimumInventory: "maker-minimum",
  targetInventory: "maker-target", maximumInventory: "maker-maximum",
};
function money(ticks) {
  const value = BigInt(ticks);
  const magnitude = value < 0n ? -value : value;
  return `${value < 0n ? "−" : ""}${(magnitude / 100n).toLocaleString()}.${(magnitude % 100n).toString().padStart(2, "0")}`;
}
function updateMakerControls() {
  const running = makerUI.data?.state === "Running";
  const disabled = !makerUI.data || makerUI.busy || makerUI.resetting;
  byId("maker-toggle").disabled = disabled || makerUI.data?.state === "Faulted";
  byId("maker-toggle").textContent = running ? "Stop maker" : "Start maker";
  byId("maker-toggle").setAttribute("aria-pressed", String(running));
  byId("maker-apply").disabled = disabled || makerUI.data?.state === "Faulted";
}
function renderMaker(data) {
  makerUI.data = data ?? null;
  updateMakerControls();
  if (!data) { byId("maker-status").textContent = "Restart server for market maker"; return; }
  if (!makerUI.initialized) {
    for (const [key, id] of Object.entries(makerFields)) byId(id).value = data[key];
    makerUI.initialized = true;
  }
  byId("maker-status").textContent = data.state + (data.buyDisabled ? " · buy disabled" : "") + (data.sellDisabled ? " · sell disabled" : "");
  byId("maker-inventory").textContent = `${data.inventory} / ${data.targetInventory}`;
  byId("maker-cash").textContent = money(data.cash);
  byId("maker-pnl").textContent = money(data.pnl);
  byId("maker-pnl").style.color = BigInt(data.pnl) < 0n ? "var(--red)" : "var(--green)";
  byId("maker-value").textContent = money(data.portfolioValue);
  for (const side of ["bid", "ask"]) {
    const quote = data[side];
    byId(`maker-${side}`).textContent = quote ? `${money(quote.price)} · ${quote.quantity} shares` : "—";
    byId(`maker-${side}`).title = quote ? `Order ${quote.id} · ${quote.price} ticks` : "No active quote";
  }
  byId("maker-account-details").textContent = `Available cash ${money(data.availableCash)} · Reserved cash ${money(data.reservedCash)} · Available shares ${data.availableShares} · Reserved shares ${data.reservedShares} · Inventory limits ${data.minimumInventory}–${data.maximumInventory}`;
  byId("maker-performance").textContent = `Buy fills ${data.buyFills} / ${data.buyVolume} shares · Sell fills ${data.sellFills} / ${data.sellVolume} shares · Excess return vs holding ${money(data.excessReturn)} · Max drawdown ${money(data.maximumDrawdown)} · Rejections ${data.rejections} · Observed inventory ${data.observedMinimumInventory}–${data.observedMaximumInventory} · Buy/sell disabled ${data.buyDisabledMs}/${data.sellDisabledMs} ms`;
  if (data.state === "Faulted") {
    byId("maker-message").textContent = `Faulted: ${data.fault} Use Clear book to reset the simulation.`;
    byId("maker-message").className = "maker-help error";
  }
  scheduleMakerPoll();
}
function scheduleMakerPoll() {
  clearTimeout(makerUI.timer); makerUI.timer = null;
  if (makerUI.data?.state !== "Running" || document.hidden || makerUI.busy || makerUI.resetting || makerUI.polling) return;
  makerUI.timer = setTimeout(async () => {
    makerUI.timer = null; makerUI.polling = true;
    try { await refresh(); }
    catch (error) { showError(error); byId("maker-message").textContent = error.message; return; }
    finally { makerUI.polling = false; }
    scheduleMakerPoll();
  }, Math.min(500, Number(makerUI.data.refreshIntervalMs)));
}
function makerSettings() {
  const settings = {};
  for (const [key, id] of Object.entries(makerFields)) {
    const text = byId(id).value.trim();
    const allowsZero = ["skewBpsPerShare", "minimumInventory", "targetInventory", "maximumInventory"].includes(key);
    settings[key] = allowsZero && text === "0" ? "0" : positiveInteger(text, key);
  }
  return settings;
}
function makerAction(action) {
  if (makerUI.busy || makerUI.resetting) return Promise.resolve();
  makerUI.busy = true; updateMakerControls(); clearTimeout(makerUI.timer);
  makerUI.pending = (async () => {
    try {
      const body = action === "stop" ? { action } : { action, ...makerSettings() };
      const result = await post("/api/market-maker", body);
      renderMaker(result.marketMaker);
      await refresh();
      byId("maker-message").textContent = action === "stop" ? "Bot quotes cancelled. Cash and holdings are preserved."
        : action === "start" ? "Maker started. Enable simulated traffic or place an order to trade against its quotes."
        : "Settings applied. Reference changes affect valuation and future quotes.";
      byId("maker-message").className = "maker-help";
      if (makerUI.data?.state === "Faulted") renderMaker(makerUI.data);
    } catch (error) {
      byId("maker-message").textContent = error.message; byId("maker-message").className = "maker-help error";
      showError(error);
    } finally {
      makerUI.busy = false; makerUI.pending = null; updateMakerControls(); scheduleMakerPoll();
    }
  })();
  return makerUI.pending;
}
byId("maker-toggle").addEventListener("click", () => makerAction(makerUI.data?.state === "Running" ? "stop" : "start"));
byId("maker-form").addEventListener("submit", (event) => { event.preventDefault(); makerAction("configure"); });
window.addEventListener("pagehide", () => {
  clearTimeout(makerUI.timer);
  if (makerUI.data?.state === "Running") fetch("/api/market-maker", {
    method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ action: "stop" }), keepalive: true,
  }).catch(() => {});
});

// A single loop submits real orders; the C++ engine creates all trades and candles.
class TrafficSimulator {
  constructor() {
    this.enabled = false;
    this.timer = null;
    this.inFlight = null;
    this.resetting = false;
    this.resting = new Map();
    this.reference = null;
    this.orders = 0;
    this.fills = 0;
    const randomWords = crypto.getRandomValues(new Uint32Array(2));
    this.nextId = (BigInt(randomWords[0]) << 32n) | BigInt(randomWords[1]) | (1n << 63n);
  }

  canRun() {
    return this.enabled && !document.hidden && !this.resetting;
  }

  updateControls() {
    const toggle = byId("traffic-toggle");
    toggle.setAttribute("aria-checked", String(this.enabled));
    toggle.classList.toggle("is-active", this.enabled);
    toggle.disabled = this.resetting || (!this.enabled && this.inFlight !== null);
    byId("traffic-toggle-label").textContent = this.enabled ? "Traffic on" : "Traffic off";
    const status = !this.enabled ? (this.inFlight ? "Stopping…" : "Off")
      : document.hidden ? "Paused · tab hidden" : "Running";
    if (byId("traffic-status").textContent !== status) byId("traffic-status").textContent = status;
    byId("traffic-counts").textContent = `${this.orders} orders · ${this.fills} fills`;
  }

  start() {
    if (this.enabled || this.inFlight || this.resetting) return;
    this.enabled = true;
    this.reference = null;
    this.updateControls();
    this.schedule(0);
  }

  async stop() {
    this.enabled = false;
    clearTimeout(this.timer);
    this.timer = null;
    this.updateControls();
    // Finish an already submitted action before allowing the book to be reset.
    if (this.inFlight) await this.inFlight;
    this.updateControls();
  }

  reset() {
    this.resting.clear();
    this.reference = null;
    this.orders = 0;
    this.fills = 0;
    byId("traffic-activity").textContent = "Turn on traffic to see resting liquidity become trades.";
    this.updateControls();
  }

  schedule(delay) {
    clearTimeout(this.timer);
    this.timer = null;
    if (!this.canRun() || this.inFlight) return;
    const interval = Number(byId("traffic-speed").value);
    this.timer = setTimeout(() => {
      this.timer = null;
      this.inFlight = this.step().catch((error) => {
        this.enabled = false;
        byId("traffic-activity").textContent = `Traffic stopped: ${error.message}`;
        showError(error);
      }).finally(() => {
        this.inFlight = null;
        this.updateControls();
        this.schedule();
      });
    }, delay ?? interval * (0.8 + Math.random() * 0.4));
  }

  randomInt(min, max) {
    return min + Math.floor(Math.random() * (max - min + 1));
  }

  clampPrice(price) {
    return price < 1n ? 1n : price > MAX_UINT64 ? MAX_UINT64 : price;
  }

  async cancel(id) {
    const result = await post("/api/cancel", { id, source: "traffic", side: this.resting.get(id)?.side });
    this.resting.delete(id);
    byId("traffic-activity").textContent = result.cancelled
      ? "Cancelled an older simulated order." : "An older simulated order was already filled or cancelled.";
  }

  async submit(side, price, quantity) {
    for (let attempt = 0; attempt < 3 && this.canRun(); attempt += 1) {
      const id = this.nextId.toString();
      this.nextId = this.nextId === MAX_UINT64 ? (1n << 63n) : this.nextId + 1n;
      let result;
      try {
        result = await post("/api/orders", { id, side, price: price.toString(), quantity: quantity.toString(), source: "traffic" });
      } catch (error) {
        if (attempt < 2 && error.message === "Order with this ID already exists.") continue;
        throw error;
      }
      if (result.resting) this.resting.set(id, { createdAt: Date.now(), side });
      this.orders += 1;
      this.fills += result.trades.length;
      const matched = result.trades.length;
      byId("traffic-activity").textContent = `${side === "buy" ? "Buy" : "Sell"} ${quantity} @ ${price} · ${matched} fill${matched === 1 ? "" : "s"}${result.resting ? " · remainder resting" : ""}`;
      return;
    }
  }

  async step() {
    if (!this.canRun()) return;
    const state = await refresh();
    if (!this.canRun()) return;

    if (this.reference === null) {
      const bid = state.bids[0]?.price;
      const ask = state.asks[0]?.price;
      this.reference = bid && ask ? (BigInt(bid) + BigInt(ask)) / 2n
        : BigInt(state.trades[0]?.price ?? bid ?? ask ?? "1000");
      this.tickSize = this.reference / 200n || 1n;
      this.floor = this.clampPrice(this.reference - 12n * this.tickSize);
      this.ceiling = this.clampPrice(this.reference + 12n * this.tickSize);
    }

    // Keep simulated liquidity bounded, and cancel only IDs created by this page.
    const oldest = this.resting.entries().next().value;
    if (oldest && (this.resting.size >= 24 || Date.now() - oldest[1].createdAt > 20000)) {
      await this.cancel(oldest[0]);
    } else if (state.bids.length && state.asks.length && this.resting.size && Math.random() < 0.12) {
      const ids = [...this.resting.keys()];
      await this.cancel(ids[this.randomInt(0, ids.length - 1)]);
    } else if (state.marketMaker?.state === "Running") {
      // Independent willingness limits: a wider bot spread is not automatically accepted.
      const reference = BigInt(state.marketMaker.referencePrice);
      const tick = reference / 200n || 1n;
      const side = Math.random() < 0.5 ? "buy" : "sell";
      const limit = this.clampPrice(reference + BigInt(this.randomInt(-8, 8)) * tick);
      await this.submit(side, limit, BigInt(this.randomInt(2, 12)));
    } else {
      const drift = BigInt(this.randomInt(-1, 1)) * this.tickSize;
      this.reference = this.clampPrice(this.reference + drift);
      this.reference = this.reference < this.floor ? this.floor : this.reference > this.ceiling ? this.ceiling : this.reference;
      // Seed missing sides first, then mix resting orders with orders that cross.
      const side = !state.bids.length ? "buy" : !state.asks.length ? "sell" : Math.random() < 0.5 ? "buy" : "sell";
      const opposite = side === "buy" ? state.asks : state.bids;
      const crosses = state.bids.length && state.asks.length && Math.random() < 0.6;
      const offset = BigInt(this.randomInt(1, 4)) * this.tickSize;
      const price = crosses ? BigInt(opposite[0].price)
        : this.reference + (side === "buy" ? -offset : offset);
      await this.submit(side, this.clampPrice(price), BigInt(this.randomInt(2, 12)));
    }
    await refresh();
  }
}

const traffic = new TrafficSimulator();
byId("traffic-toggle").addEventListener("click", () => {
  if (traffic.enabled) traffic.stop();
  else traffic.start();
});
byId("traffic-speed").addEventListener("change", () => traffic.schedule(0));
document.addEventListener("visibilitychange", () => {
  traffic.updateControls();
  traffic.schedule(0);
  scheduleMakerPoll();
});
window.addEventListener("pagehide", () => traffic.stop());

function syncOrderType() {
  const market = byId("order-type").value === "market";
  byId("order-price-field").hidden = market;
  byId("order-price").disabled = market;
  byId("order-price").required = !market;
  byId("order-form").classList.toggle("is-market", market);
  byId("place-button-label").textContent = market ? "Place market order" : "Place limit order";
  byId("order-type-help").textContent = market
    ? "Fills immediately at the best available prices, possibly across several levels. Any unfilled quantity is cancelled."
    : "Limit orders fill at your price or better; any remainder rests in the book.";
}
byId("order-type").addEventListener("change", syncOrderType);
syncOrderType();

byId("order-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const button = byId("place-button");
  button.disabled = true;
  try {
    const id = positiveInteger(byId("order-id").value, "ID");
    const quantity = positiveInteger(byId("order-quantity").value, "Quantity");
    const side = document.querySelector('input[name="side"]:checked').value;
    const type = byId("order-type").value;
    const order = { id, side, quantity, type };
    if (type === "limit") order.price = positiveInteger(byId("order-price").value, "Price");
    const result = await post("/api/orders", order);
    await refresh();
    const count = result.trades.length;
    const resultText = count === 0 ? "No trades." : `${count} trade${count === 1 ? "" : "s"} matched.`;
    if (type === "market") {
      const text = `Market order ${id}: ${result.filledQuantity} filled across ${count} trade${count === 1 ? "" : "s"}.`
        + (BigInt(result.cancelledQuantity) > 0n ? ` ${result.cancelledQuantity} unfilled cancelled.` : "");
      setMessage(text, BigInt(result.filledQuantity) > 0n ? "success" : "");
    } else setMessage(`${resultText} Order ${id} ${result.resting ? "is resting in the book" : "was fully filled"}.`, "success");
    if (result.resting) byId("cancel-id").value = id;
    if (BigInt(id) < MAX_UINT64) byId("order-id").value = (BigInt(id) + 1n).toString();
  } catch (error) {
    showError(error);
  } finally {
    button.disabled = false;
  }
});

byId("cancel-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  try {
    const id = positiveInteger(byId("cancel-id").value, "ID");
    const result = await post("/api/cancel", { id });
    await refresh();
    setMessage(result.cancelled ? `Order ${id} was cancelled.` : `Order ${id} is not active.`, result.cancelled ? "success" : "");
  } catch (error) {
    showError(error);
  }
});

byId("reset-button").addEventListener("click", async () => {
  if (!window.confirm("Clear the book, trades, and candles, stop both bots, and reset the maker to its initial virtual cash and shares? Saved CSV trades remain.")) return;
  const button = byId("reset-button");
  button.disabled = true;
  traffic.resetting = true;
  makerUI.resetting = true;
  updateMakerControls();
  clearTimeout(makerUI.timer);
  try {
    await traffic.stop();
    if (makerUI.pending) await makerUI.pending;
    await post("/api/reset", {});
    traffic.reset();
    await refresh();
    setMessage("Book cleared. Traffic and market maker stopped; virtual account reset.", "success");
  } catch (error) {
    showError(error);
  } finally {
    button.disabled = false;
    traffic.resetting = false;
    traffic.updateControls();
    makerUI.resetting = false;
    updateMakerControls();
  }
});

refresh().catch(showError);
