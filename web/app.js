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
    if (value >= size) return `${value / size}${suffix}`;
  }
  return value.toString();
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
}

function showError(error) {
  setMessage(error.message, "error");
  if (error instanceof TypeError) {
    const connection = byId("connection");
    connection.textContent = "Server unavailable";
    connection.className = "connection offline";
  }
}

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
  if (!window.confirm("Clear all resting orders and recent trades?")) return;
  try {
    await post("/api/reset", {});
    await refresh();
    setMessage("The order book is empty.", "success");
  } catch (error) {
    showError(error);
  }
});

refresh().catch(showError);
