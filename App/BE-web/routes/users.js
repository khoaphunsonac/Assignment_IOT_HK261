const express = require("express");
const { authenticateToken } = require("../middleware/auth");
const { notImplemented } = require("../utils/response");

const router = express.Router();

router.use(authenticateToken);

router.get("/profile", notImplemented);
router.put("/profile", notImplemented);

module.exports = router;
