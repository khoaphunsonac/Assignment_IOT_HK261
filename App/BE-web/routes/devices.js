const express = require("express");
const { authenticateToken } = require("../middleware/auth");
const { notImplemented } = require("../utils/response");

const router = express.Router();

router.use(authenticateToken);

router.get("/", notImplemented);
router.post("/", notImplemented);
router.get("/:id", notImplemented);
router.put("/:id/toggle", notImplemented);

module.exports = router;
