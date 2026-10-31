const request = require("supertest");
const app = require("../app");

describe("App skeleton", () => {
    it("returns health status", async () => {
        const res = await request(app).get("/health");
        expect(res.statusCode).toBe(200);
        expect(res.body.status).toBe("OK");
    });

    it("returns 404 for unknown routes", async () => {
        const res = await request(app).get("/api/unknown");
        expect(res.statusCode).toBe(404);
    });

    it("returns 501 for routes not implemented yet", async () => {
        const res = await request(app).post("/api/auth/login");
        expect(res.statusCode).toBe(501);
    });

    it("requires a token on protected routes", async () => {
        const res = await request(app).get("/api/rooms");
        expect(res.statusCode).toBe(401);
    });
});
