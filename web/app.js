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
    addRow(body, ["No resting orders", "", ""], true);
    return;
  }
  for (const level of levels) {
    addRow(body, [level.price, level.quantity, level.orderCount]);
  }
}

function renderTrades(trades) {
  byId("trades-count").textContent = `${trades.length} trade${trades.length === 1 ? "" : "s"}`;
  const body = byId("trades-body");
  body.replaceChildren();
  if (trades.length === 0) {
    addRow(body, ["No trades yet", "", "", ""], true);
    return;
  }
  for (const trade of trades) {
    addRow(body, [trade.makerId, trade.takerId, trade.price, trade.quantity]);
  }
}

function renderCandles(candles) {
  renderCandleChart(candles);
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

function renderCandleChart(candles) {
  const chart = byId("candle-chart");
  const empty = byId("candle-empty");
  const plot = byId("candle-plot");
  const hover = byId("candle-hover");
  const readout = byId("candle-readout");
  plot.replaceChildren();
  hover.replaceChildren();

  if (candles.length === 0) {
    chart.setAttribute("hidden", "");
    empty.hidden = false;
    readout.textContent = "Waiting for the first trade";
    return;
  }

  chart.removeAttribute("hidden");
  empty.hidden = true;
  const visible = candles.slice(-48);
  const left = 38;
  const right = 846;
  const priceTop = 24;
  const priceBottom = 300;
  const volumeTop = 337;
  const volumeBottom = 412;
  const timeLabelY = 442;
  // Keep a short history compact instead of stretching its candles across the plot.
  const slotWidth = Math.min(28, (right - left) / visible.length);
  const startX = left + ((right - left) - visible.length * slotWidth) / 2;
  const x = (index) => startX + (index + 0.5) * slotWidth;
  const bodyWidth = slotWidth * 0.72;

  const lowest = visible.reduce((low, candle) => {
    const price = BigInt(candle.low);
    return price < low ? price : low;
  }, BigInt(visible[0].low));
  const highest = visible.reduce((high, candle) => {
    const price = BigInt(candle.high);
    return price > high ? price : high;
  }, BigInt(visible[0].high));
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
  }, 1n);
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
  const lastCloseY = yPrice(BigInt(last.close));
  plot.appendChild(svgElement("line", { x1: left, y1: lastCloseY, x2: right, y2: lastCloseY, class: "candle-last-price" }));

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
    const barHeight = Math.max(1, Number(BigInt(candle.volume) * volumeHeight / maxVolume));

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

  showCandle(visible.length - 1);
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
}

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
  renderLevels("bids-body", state.bids);
  renderLevels("asks-body", state.asks);
  renderDepth(state);
  renderTrades(state.trades);
  renderCandles(state.candles ?? []);
  byId("best-bid").textContent = state.bids[0]?.price ?? "—";
  byId("best-ask").textContent = state.asks[0]?.price ?? "—";
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
    const result = await post("/api/cancel", { id });
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
        result = await post("/api/orders", { id, side, price: price.toString(), quantity: quantity.toString() });
      } catch (error) {
        if (attempt < 2 && error.message === "Order with this ID already exists.") continue;
        throw error;
      }
      if (result.resting) this.resting.set(id, Date.now());
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
    if (oldest && (this.resting.size >= 24 || Date.now() - oldest[1] > 20000)) {
      await this.cancel(oldest[0]);
    } else if (state.bids.length && state.asks.length && this.resting.size && Math.random() < 0.12) {
      const ids = [...this.resting.keys()];
      await this.cancel(ids[this.randomInt(0, ids.length - 1)]);
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
});
window.addEventListener("pagehide", () => traffic.stop());

byId("order-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const button = byId("place-button");
  button.disabled = true;
  try {
    const id = positiveInteger(byId("order-id").value, "ID");
    const price = positiveInteger(byId("order-price").value, "Price");
    const quantity = positiveInteger(byId("order-quantity").value, "Quantity");
    const side = document.querySelector('input[name="side"]:checked').value;
    const result = await post("/api/orders", { id, side, price, quantity });
    await refresh();
    const count = result.trades.length;
    const resultText = count === 0 ? "No trades." : `${count} trade${count === 1 ? "" : "s"} matched.`;
    setMessage(`${resultText} Order ${id} ${result.resting ? "is resting in the book" : "was fully filled"}.`, "success");
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
  if (!window.confirm("Clear resting orders, recent trades, and live candles? Saved CSV trades will remain.")) return;
  const button = byId("reset-button");
  button.disabled = true;
  traffic.resetting = true;
  try {
    await traffic.stop();
    await post("/api/reset", {});
    traffic.reset();
    await refresh();
    setMessage("The order book is empty. Simulated traffic is off.", "success");
  } catch (error) {
    showError(error);
  } finally {
    button.disabled = false;
    traffic.resetting = false;
    traffic.updateControls();
  }
});

refresh().catch(showError);
