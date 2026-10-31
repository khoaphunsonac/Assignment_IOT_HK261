const { verifyToken } = require("../utils/jwt");
const { sendError } = require("../utils/response");

const authenticateToken = (req, res, next) => {
    const token = req.headers.authorization?.split(" ")[1];
    if (!token) {
        return sendError(res, "Access token is required", 401);
    }
    try {
        req.user = verifyToken(token);
        next();
    } catch (error) {
        return sendError(res, "Invalid or expired token", 401);
    }
};

module.exports = { authenticateToken };
