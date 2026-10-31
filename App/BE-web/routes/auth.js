const express = require("express");
const { authenticateToken } = require("../middleware/auth");
const { notImplemented } = require("../utils/response");

const router = express.Router();

router.post("/register", notImplemented);
router.post("/login", notImplemented);
router.get("/me", authenticateToken, notImplemented);

module.exports = router;
