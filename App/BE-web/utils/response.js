const sendSuccess = (res, message, data = null, statusCode = 200) => {
    return res.status(statusCode).json({ success: true, message, ...(data && { data }) });
};

const sendError = (res, message, statusCode = 500) => {
    return res.status(statusCode).json({ success: false, message });
};

const notImplemented = (req, res) => sendError(res, "Not implemented", 501);

module.exports = { sendSuccess, sendError, notImplemented };
