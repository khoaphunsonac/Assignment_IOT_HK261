const { Sequelize } = require("sequelize");

const sequelize = new Sequelize(
    process.env.DB_NAME || "iot_assignment",
    process.env.DB_USER || "root",
    process.env.DB_PASSWORD || "",
    {
        host: process.env.DB_HOST || "localhost",
        port: process.env.DB_PORT || 3306,
        dialect: "mysql",
        logging: false,
        define: {
            timestamps: true,
            underscored: true,
            freezeTableName: true,
        },
    }
);

const connectDB = async () => {
    if (!process.env.DB_HOST) {
        console.warn("DB_HOST is not set, running without a database");
        return;
    }
    try {
        await sequelize.authenticate();
        console.log("MySQL connected");
    } catch (error) {
        console.error("Database connection error:", error.message);
        process.exit(1);
    }
};

module.exports = { sequelize, connectDB };
