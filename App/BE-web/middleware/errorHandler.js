const errorHandler = (err, req, res, next) => {
    console.error("Error:", err.message);
    res.status(err.statusCode || 500).json({
        success: false,
        message: err.message || "Server Error",
    });
};

const notFound = (req, res, next) => {
    const error = new Error(`Not Found - ${req.originalUrl}`);
    error.statusCode = 404;
    next(error);
};

module.exports = { errorHandler, notFound };
