// obj_to_cpp.ts
if (Deno.args.length < 2) {
  console.error("Usage: deno run --allow-read --allow-write obj_to_cpp.ts <input.obj> <output.hpp>");
  Deno.exit(1);
}

const [inputPath, outputPath] = Deno.args;

// OBJから読み込んだ生の属性プール
const rawPositions: [number, number, number][] = [];
const rawUvs: [number, number][] = [];
const rawNormals: [number, number, number][] = [];

// ユニークな頂点の組み合わせ（"v/vt/vn" の文字列で管理）とインデックス配列
const uniqueVertices: string[] = [];
const indices: number[] = [];

const text = await Deno.readTextFile(inputPath);
const lines = text.split(/\r?\n/);

for (const line of lines) {
  const trimmed = line.trim();
  if (trimmed.startsWith("#") || trimmed === "") continue;

  const parts = trimmed.split(/\s+/);
  const type = parts[0];

  if (type === "v") {
    rawPositions.push([parseFloat(parts[1]), parseFloat(parts[2]), parseFloat(parts[3])]);
  } else if (type === "vt") {
    rawUvs.push([parseFloat(parts[1]), parseFloat(parts[2])]);
  } else if (type === "vn") {
    rawNormals.push([parseFloat(parts[1]), parseFloat(parts[2]), parseFloat(parts[3])]);
  } else if (type === "f") {
    // 多角形対応（ポリゴンファン形式で三角形化）
    for (let i = 1; i < parts.length - 2; i++) {
      const faceTokens = [parts[1], parts[i + 1], parts[i + 2]];

      for (const token of faceTokens) {
        let idx = uniqueVertices.indexOf(token);
        // 見つからなければ新しいユニーク頂点として登録
        if (idx === -1) {
          uniqueVertices.push(token);
          idx = uniqueVertices.length - 1;
        }
        indices.push(idx);
      }
    }
  }
}

const meshName = inputPath.split("/").pop()?.replace(".obj", "") || "compiled_mesh";
const fmt = (n: number): string => Number.isInteger(n) ? `${n}.0f` : `${n}f`;

let cppContent = `// Generated from ${inputPath} via astream asset compiler\n`;
cppContent += `#include <cstdint>\n\n`;
cppContent += `inline constexpr uint32_t ${meshName}_vertex_count = ${uniqueVertices.length};\n`;
cppContent += `inline constexpr uint32_t ${meshName}_index_count = ${indices.length};\n\n`;

// 各属性の書き出し用配列を準備
const finalPositions: string[] = [];
const finalNormals: string[] = [];
const finalUvs: string[] = [];

for (const token of uniqueVertices) {
  const [vIdxStr, vtIdxStr, vnIdxStr] = token.split("/");

  const p = rawPositions[parseInt(vIdxStr) - 1] || [0, 0, 0];
  finalPositions.push(`${fmt(p[0])}, ${fmt(p[1])}, ${fmt(p[2])}`);

  const n = vnIdxStr ? (rawNormals[parseInt(vnIdxStr) - 1] || [0, 1, 0]) : [0, 1, 0];
  finalNormals.push(`${fmt(n[0])}, ${fmt(n[1])}, ${fmt(n[2])}`);

  const u = vtIdxStr ? (rawUvs[parseInt(vtIdxStr) - 1] || [0, 0]) : [0, 0];
  finalUvs.push(`${fmt(u[0])}, ${fmt(u[1])}`);
}

// 1. Positions 配列の書き出し
cppContent += `inline constexpr float ${meshName}_positions[] = {\n    `;
for (let i = 0; i < finalPositions.length; i++) {
  cppContent += `${finalPositions[i]}, `;
  if ((i + 1) % 3 === 0) cppContent += "\n    "; // 3頂点ごとに改行
}
cppContent += `\n};\n\n`;

// 2. Normals 配列の書き出し
cppContent += `inline constexpr float ${meshName}_normals[] = {\n    `;
for (let i = 0; i < finalNormals.length; i++) {
  cppContent += `${finalNormals[i]}, `;
  if ((i + 1) % 3 === 0) cppContent += "\n    ";
}
cppContent += `\n};\n\n`;

// 3. Texcoords (UV) 配列の書き出し
cppContent += `inline constexpr float ${meshName}_texcoords[] = {\n    `;
for (let i = 0; i < finalUvs.length; i++) {
  cppContent += `${finalUvs[i]}, `;
  if ((i + 1) % 4 === 0) cppContent += "\n    "; // 4頂点ごとに改行
}
cppContent += `\n};\n\n`;

// 4. Indices 配列の書き出し
cppContent += `inline constexpr int ${meshName}_indices[] = {\n    `;
for (let i = 0; i < indices.length; i++) {
  cppContent += `${indices[i]}, `;
  if ((i + 1) % 12 === 0) cppContent += "\n    "; // 12インデックスごとに改行
}
cppContent += `\n};\n`;

const outputDir = outputPath.substring(0, outputPath.lastIndexOf("/"));

// ディレクトリが存在しなければ再帰的に作成する
await Deno.mkdir(outputDir, { recursive: true });

await Deno.writeTextFile(outputPath, cppContent);
console.log(`Successfully compiled asset with Indices: ${inputPath} -> ${outputPath}`);
