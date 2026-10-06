import { createHash } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";

const payloadFile = "FreelancerOwnedWallCopy.dll";
const payloadRelativePath = path.join("native", payloadFile);
const payloadSha256 = "EDF69AE090BA08C441DD2CD7A95B147A741999B2D015283C781F716EF2959C5B";
const sdkCoreFile = "ZHMModSDK.dll";
const sdkCoreSha256 = "50D45AF5D3B6B345F6E6DECFB2E0DCA2F5F8EE353918BC01F4C03118744817A7";
const gameImageName = "HITMAN3.exe";
const backupSuffix = ".bak";
const safehouseTempHash = "00A9B93A825F5BDC";
const safehouseSubEntityIndex = 2217;
const badPropertyId = 0xD95905B8;
const correctPropertyId = 12580261;

type ProcessResult = { status: number | null; stdout?: string; stderr?: string; error?: Error };
type ProcessRunner = (command: string, args: string[], options: Record<string, unknown>) => ProcessResult;
type TestDependencies = { spawnSync?: ProcessRunner };

export const cachingPolicy = { affected: [safehouseTempHash] };

type PropertyIdRepair = { status: "changed" | "already-fixed" | "not-found"; buffer: Buffer; offset?: number };

export function repairSafehousePropertyIdBytes(source: Buffer): PropertyIdRepair {
    const badId = Buffer.alloc(4);
    badId.writeUInt32LE(badPropertyId);
    const correctId = Buffer.alloc(4);
    correctId.writeUInt32LE(correctPropertyId);

    const firstBad = source.indexOf(badId);
    const lastBad = source.lastIndexOf(badId);
    if (firstBad >= 0) {
        if (firstBad !== lastBad)
            throw new Error("Safehouse TEMP contains multiple copies of the known bad property ID; refusing an ambiguous repair.");
        const repaired = Buffer.from(source);
        repaired.writeUInt32LE(correctPropertyId, firstBad);
        return { status: "changed", buffer: repaired, offset: firstBad };
    }

    if (source.indexOf(correctId) >= 0)
        return { status: "already-fixed", buffer: source };
    return { status: "not-found", buffer: source };
}

function findStagedSafehouseTemp(stagingRoot: string): string | undefined {
    if (!fs.existsSync(stagingRoot)) return undefined;
    const matches: string[] = [];
    for (const entry of fs.readdirSync(stagingRoot, { withFileTypes: true })) {
        if (!entry.isDirectory() || !/^chunk\d+(?:patch\d+)?$/i.test(entry.name)) continue;
        const candidate = path.join(stagingRoot, entry.name, safehouseTempHash + ".TEMP");
        if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) matches.push(candidate);
    }
    if (matches.length > 1)
        throw new Error("More than one staged safehouse TEMP exists; refusing to choose between them.");
    return matches[0];
}

function runResourceTool(runner: ProcessRunner, resourceToolPath: string, args: string[]): void {
    const result = runner(resourceToolPath, args, {
        cwd: process.cwd(), encoding: "utf8", windowsHide: true, shell: false, maxBuffer: 4 * 1024 * 1024,
    });
    if (result.error || result.status !== 0)
        throw new Error("ResourceTool failed (" + String(result.status) + "): " + (result.stderr ?? result.error?.message ?? "no diagnostic output"));
}

function requireExpectedMaterialProperty(decoded: any, expectedId: number): void {
    const entity = decoded?.subEntities?.[safehouseSubEntityIndex];
    if (!entity || !Array.isArray(entity.propertyValues))
        throw new Error("Decoded safehouse TEMP no longer has the verified target entity at sub-entity index " + safehouseSubEntityIndex + ".");

    const property = entity.propertyValues.find((entry: any) => entry?.nPropertyID === expectedId);
    if (!property || property.value?.$type !== "ZRuntimeResourceID" ||
        property.value?.$val?.m_IDHigh !== 0 || property.value?.$val?.m_IDLow !== 4478)
        throw new Error("Safehouse target property does not match the verified carpet material override; refusing to alter it.");

    const matches = decoded.subEntities.flatMap((item: any) => Array.isArray(item?.propertyValues) ? item.propertyValues : [])
        .filter((entry: any) => entry?.nPropertyID === expectedId);
    if (matches.length !== 1)
        throw new Error("Decoded safehouse TEMP has an unexpected count of the target property ID (" + matches.length + ").");
}

export async function repairStagedSafehouseProperty(context: any, modAPI: any, deps?: TestDependencies): Promise<void> {
    const stagingRoot = path.join(process.cwd(), "staging");
    const stagedTemp = findStagedSafehouseTemp(stagingRoot);
    if (!stagedTemp) {
        await info(modAPI, "Safehouse TEMP was not staged; numeric property repair skipped.");
        return;
    }

    const resourceToolPath = path.join(process.cwd(), "Third-Party", "ResourceTool.exe");
    requirePath(resourceToolPath, "SMF ResourceTool");
    if (!context?.tempFolder || !path.isAbsolute(context.tempFolder))
        throw new Error("SMF did not provide an absolute script tempFolder for the safehouse validation files.");
    fs.mkdirSync(context.tempFolder, { recursive: true });
    const validationDir = fs.mkdtempSync(path.join(context.tempFolder, "wall-copy-safehouse-"));
    const runner = deps?.spawnSync ?? spawnSync;
    const execute = (args: string[]) => runResourceTool(runner, resourceToolPath, args);
    const beforeJson = path.join(validationDir, "before.json");
    const candidateTemp = path.join(validationDir, "candidate.TEMP");
    const afterJson = path.join(validationDir, "after.json");

    try {
        execute(["HM3", "convert", "TEMP", stagedTemp, beforeJson]);
        const before = JSON.parse(fs.readFileSync(beforeJson, "utf8"));
        const target = before?.subEntities?.[safehouseSubEntityIndex]?.propertyValues;
        if (!Array.isArray(target))
            throw new Error("Decoded safehouse TEMP is missing the verified target property list.");
        const badMatches = target.filter((entry: any) => entry?.nPropertyID === badPropertyId);
        const correctMatches = target.filter((entry: any) => entry?.nPropertyID === correctPropertyId);

        if (badMatches.length === 0 && correctMatches.length === 1) {
            requireExpectedMaterialProperty(before, correctPropertyId);
            await info(modAPI, "Safehouse carpet material property already has numeric ID " + correctPropertyId + ".");
            return;
        }
        if (badMatches.length !== 1 || correctMatches.length !== 0)
            throw new Error("Decoded safehouse TEMP does not contain exactly one verified bad property ID at the target entity; refusing repair.");
        requireExpectedMaterialProperty(before, badPropertyId);

        const originalBytes = fs.readFileSync(stagedTemp);
        if (originalBytes.subarray(0, 4).toString("ascii") !== "BIN1")
            throw new Error("Staged safehouse TEMP does not have the verified BIN1 header; refusing a binary field edit.");
        const repaired = repairSafehousePropertyIdBytes(originalBytes);
        if (repaired.status !== "changed")
            throw new Error("Decoded safehouse TEMP identifies the bad property ID but the binary does not contain one unique matching field.");
        if (repaired.buffer.length !== originalBytes.length)
            throw new Error("Safehouse TEMP repair unexpectedly changed the resource size.");

        fs.writeFileSync(candidateTemp, repaired.buffer, { flag: "wx" });
        execute(["HM3", "convert", "TEMP", candidateTemp, afterJson]);
        const after = JSON.parse(fs.readFileSync(afterJson, "utf8"));
        requireExpectedMaterialProperty(after, correctPropertyId);
        if (after.subEntities[safehouseSubEntityIndex].propertyValues.some((entry: any) => entry?.nPropertyID === badPropertyId))
            throw new Error("Decoded candidate still contains the bad property ID.");

        fs.renameSync(stagedTemp, stagedTemp + ".wall-copy-repair-original");
        try {
            fs.renameSync(candidateTemp, stagedTemp);
        } catch (error) {
            fs.renameSync(stagedTemp + ".wall-copy-repair-original", stagedTemp);
            throw error;
        }
        fs.unlinkSync(stagedTemp + ".wall-copy-repair-original");
        await info(modAPI, "Repaired the safehouse carpet property ID to " + correctPropertyId + "; ResourceTool decoded the preserved material override.");
    } finally {
        fs.rmSync(validationDir, { recursive: true, force: true });
    }
}

function sha256(filePath: string): string {
    return createHash("sha256").update(fs.readFileSync(filePath)).digest("hex").toUpperCase();
}

function requirePath(filePath: string, label: string): void {
    if (!fs.existsSync(filePath) || !fs.statSync(filePath).isFile())
        throw new Error(label + " is missing: " + filePath);
}

function resolvePaths(context: any) {
    if (!context?.modRoot || !context?.config?.retailPath)
        throw new Error("SMF did not provide modRoot and config.retailPath.");
    if (!path.isAbsolute(context.modRoot) || !path.isAbsolute(context.config.retailPath))
        throw new Error("SMF modRoot and config.retailPath must be absolute paths.");
    if (path.basename(path.resolve(context.config.retailPath)).toLowerCase() !== "retail")
        throw new Error("Refusing installation because config.retailPath does not end in Retail.");

    const retailPath = path.resolve(context.config.retailPath);
    const modsPath = path.join(retailPath, "mods");
    return {
        payloadPath: path.join(context.modRoot, payloadRelativePath),
        sdkCorePath: path.join(retailPath, sdkCoreFile),
        pluginPath: path.join(modsPath, payloadFile),
        backupPrefix: path.join(modsPath, payloadFile),
        backupPath: path.join(modsPath, payloadFile + backupSuffix),
    };
}

function verifyPayload(payloadPath: string): void {
    requirePath(payloadPath, "Bundled native helper");
    const actual = sha256(payloadPath);
    if (actual !== payloadSha256)
        throw new Error("Bundled native helper hash mismatch (expected " + payloadSha256 + ", found " + actual + ").");
}

function requireGameClosed(runner: ProcessRunner): void {
    const result = runner("tasklist", ["/FI", "IMAGENAME eq " + gameImageName, "/FO", "CSV", "/NH"], {
        encoding: "utf8", windowsHide: true, shell: false,
    });
    if (result.error || result.status !== 0)
        throw new Error("Could not verify that " + gameImageName + " is closed; refusing native file changes.");
    const rows = (result.stdout ?? "").split(/\r?\n/);
    if (rows.some((row) => new RegExp('^"' + gameImageName + '"[,]', "i").test(row.trim())))
        throw new Error("Close " + gameImageName + " before deploying the wall-copy helper.");
}

function preflight(context: any, deps?: TestDependencies) {
    const paths = resolvePaths(context);
    verifyPayload(paths.payloadPath);
    requirePath(paths.sdkCorePath, "ZHM Mod SDK core");
    const sdkHash = sha256(paths.sdkCorePath);
    if (sdkHash !== sdkCoreSha256)
        throw new Error("ZHM Mod SDK core hash mismatch (expected " + sdkCoreSha256 + ", found " + sdkHash + "); install the pinned SDK 4.1.1 core first.");
    requireGameClosed(deps?.spawnSync ?? spawnSync);
    if (fs.existsSync(paths.pluginPath) && sha256(paths.pluginPath) !== payloadSha256) {
        paths.backupPath = paths.backupPrefix + "." + sha256(paths.pluginPath) + backupSuffix;
        if (fs.existsSync(paths.backupPath) && sha256(paths.backupPath) !== sha256(paths.pluginPath))
            throw new Error("Refusing to overwrite hash-named backup collision at " + paths.backupPath + ".");
    }
    return paths;
}

async function info(modAPI: any, message: string): Promise<void> {
    if (modAPI?.logger?.info) await modAPI.logger.info(message);
}

async function reportFailure(modAPI: any, error: unknown): Promise<void> {
    const message = error instanceof Error ? error.message : String(error);
    if (modAPI?.logger?.error) await modAPI.logger.error(message, false);
}

export async function analysis(context: any, modAPI: any): Promise<void> {
    try {
        const paths = resolvePaths(context);
        verifyPayload(paths.payloadPath);
        await info(modAPI, "Wall-copy helper payload is present and matches the package hash.");
    } catch (error) {
        await reportFailure(modAPI, error);
        throw error;
    }
}

export async function beforeDeploy(context: any, modAPI: any, deps?: TestDependencies): Promise<void> {
    try {
        preflight(context, deps);
        await info(modAPI, "Wall-copy deployment preflight passed: pinned SDK core present and HITMAN 3 closed.");
    } catch (error) {
        await reportFailure(modAPI, error);
        throw error;
    }
}

export async function afterDeploy(context: any, modAPI: any, deps?: TestDependencies): Promise<void> {
    let paths;
    try {
        paths = preflight(context, deps);
    } catch (error) {
        await reportFailure(modAPI, error);
        throw error;
    }
    try {
        await repairStagedSafehouseProperty(context, modAPI, deps);
    } catch (error) {
        await reportFailure(modAPI, error);
        throw error;
    }
    if (fs.existsSync(paths.pluginPath) && sha256(paths.pluginPath) === payloadSha256) {
        await info(modAPI, "Wall-copy helper already matches this package.");
        return;
    }

    fs.mkdirSync(path.dirname(paths.pluginPath), { recursive: true });
    let previousHash: string | undefined;
    let backupCreated = false;
    let backupReady = false;
    let displacedPath: string | undefined;
    let installedByUs = false;
    const temporaryPath = paths.pluginPath + "." + process.pid + "." + Date.now() + ".deploy-tmp";
    try {
        if (fs.existsSync(paths.pluginPath)) {
            previousHash = sha256(paths.pluginPath);
            paths.backupPath = paths.backupPrefix + "." + previousHash + backupSuffix;
            if (!fs.existsSync(paths.backupPath)) {
                fs.copyFileSync(paths.pluginPath, paths.backupPath, fs.constants.COPYFILE_EXCL);
                backupCreated = true;
            }
            if (sha256(paths.backupPath) !== previousHash)
                throw new Error("The hash-named native helper backup failed its hash check.");
            backupReady = true;
        }

        fs.copyFileSync(paths.payloadPath, temporaryPath, fs.constants.COPYFILE_EXCL);
        if (sha256(temporaryPath) !== payloadSha256)
            throw new Error("The staged native helper failed its hash check.");

        if (fs.existsSync(paths.pluginPath)) {
            displacedPath = paths.pluginPath + "." + process.pid + "." + Date.now() + ".replace-tmp";
            fs.renameSync(paths.pluginPath, displacedPath);
        }
        fs.renameSync(temporaryPath, paths.pluginPath);
        installedByUs = true;
        if (sha256(paths.pluginPath) !== payloadSha256)
            throw new Error("Installed native helper failed its hash check.");
        if (displacedPath && fs.existsSync(displacedPath)) fs.unlinkSync(displacedPath);
        await info(modAPI, backupReady
            ? "Installed the wall-copy helper; the prior helper is preserved at " + paths.backupPath + "."
            : "Installed the wall-copy helper.");
    } catch (error) {
        if (displacedPath && fs.existsSync(displacedPath)) {
            if (fs.existsSync(paths.pluginPath)) fs.unlinkSync(paths.pluginPath);
            fs.renameSync(displacedPath, paths.pluginPath);
        } else if (backupReady && previousHash && fs.existsSync(paths.backupPath)) {
            if (fs.existsSync(paths.pluginPath) && sha256(paths.pluginPath) !== previousHash)
                fs.unlinkSync(paths.pluginPath);
            if (!fs.existsSync(paths.pluginPath))
                fs.copyFileSync(paths.backupPath, paths.pluginPath, fs.constants.COPYFILE_EXCL);
        } else if (installedByUs && !previousHash && fs.existsSync(paths.pluginPath) &&
            sha256(paths.pluginPath) !== payloadSha256) {
            fs.unlinkSync(paths.pluginPath);
        }
        if (backupCreated && !backupReady && fs.existsSync(paths.backupPath)) fs.unlinkSync(paths.backupPath);
        if (fs.existsSync(temporaryPath)) fs.unlinkSync(temporaryPath);
        const failure = new Error("Wall-copy helper installation failed: " + (error instanceof Error ? error.message : String(error)));
        await reportFailure(modAPI, failure);
        throw failure;
    }
}




