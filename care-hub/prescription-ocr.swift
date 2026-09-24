import Foundation
import Vision
import ImageIO

// Runs locally. Prescription photos and recognized text never leave this Mac.
guard CommandLine.arguments.count == 2 else { exit(2) }
let url = URL(fileURLWithPath: CommandLine.arguments[1])
let request = VNRecognizeTextRequest()
request.recognitionLevel = .accurate
request.usesLanguageCorrection = false
request.automaticallyDetectsLanguage = true
do {
  let handler = VNImageRequestHandler(url: url, options: [:])
  try handler.perform([request])
  let lines = (request.results ?? []).compactMap { $0.topCandidates(1).first }
  let result: [String: Any] = ["text": lines.map { $0.string }.joined(separator: "\n"), "confidence": lines.map { Double($0.confidence) }.min() ?? 0]
  let data = try JSONSerialization.data(withJSONObject: result)
  FileHandle.standardOutput.write(data)
} catch {
  FileHandle.standardError.write(Data("Could not read this image. Try a clear JPEG or PNG.\n".utf8))
  exit(1)
}
