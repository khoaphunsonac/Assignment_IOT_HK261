const express = require("express");
const cors = require("cors");
const helmet = require("helmet");
const morgan = require("morgan");
const compression = require("compression");
const rateLimit = require("express-rate-limit");

const { errorHandler, notFound } = require("./middleware/errorHandler");

const app = express();

app.use(helmet());
app.use(
    rateLimit({
        windowMs: parseInt(process.env.RATE_LIMIT_WINDOW_MS) || 15 * 60 * 1000,
        max: parseInt(process.env.RATE_LIMIT_MAX_REQUESTS) || 100,
    })
);
app.use(
    cors({
        origin: process.env.FRONTEND_URL || "http://localhost:3000",
        credentials: true,
    })
);
app.use(compression());
if (process.env.NODE_ENV !== "test") {
    app.use(morgan(process.env.NODE_ENV === "development" ? "dev" : "combined"));
}
app.use(express.json({ limit: "1mb" }));
app.use(express.urlencoded({ extended: true, limit: "1mb" }));

app.get("/health", (req, res) => {
    res.status(200).json({
        status: "OK",
        timestamp: new Date().toISOString(),
        uptime: process.uptime(),
    });
});

app.use("/api/auth", require("./routes/auth"));
app.use("/api/users", require("./routes/users"));
app.use("/api/rooms", require("./routes/rooms"));
app.use("/api/devices", require("./routes/devices"));
app.use("/api/environment", require("./routes/environment"));
app.use("/api/notifications", require("./routes/notifications"));
app.use("/api/usage-history", require("./routes/usageHistory"));
app.use("/api/coreiot", require("./routes/coreiot"));

app.use(notFound);
app.use(errorHandler);

module.exports = app;
