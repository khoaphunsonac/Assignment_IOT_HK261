const express = require("express");
const { authenticateToken } = require("../middleware/auth");
const { notImplemented } = require("../utils/response");

const router = express.Router();

router.use(authenticateToken);

router.get("/", notImplemented);
router.post("/", notImplemented);

module.exports = router;
