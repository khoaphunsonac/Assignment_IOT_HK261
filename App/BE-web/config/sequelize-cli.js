require("dotenv").config();

const config = {
    username: process.env.DB_USER,
    password: process.env.DB_PASSWORD,
    database: process.env.DB_NAME,
    host: process.env.DB_HOST,
    port: process.env.DB_PORT || 3306,
    dialect: "mysql",
    seederStorage: "sequelize",
};

module.exports = {
    development: config,
    test: config,
    production: config,
};
