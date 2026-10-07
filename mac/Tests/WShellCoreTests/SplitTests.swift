import XCTest
import CoreGraphics
@testable import WShellCore

final class SplitTests: XCTestCase {
    func testAliasAndColorSurviveBackup() throws {
        var host = try Record.quick("user@example.com")
        XCTAssertEqual(host.displayName, host.name)
        host["WookAlias"] = "운영 API"; host["WookTabColor"] = "4385be"
        try host.validate()
        let restored = try Backup.decode(Backup.encode([host]))[0]
        XCTAssertEqual(restored.displayName, "운영 API"); XCTAssertEqual(restored.tabColor, 0x4385be)
        host["WookTabColor"] = "oops"; XCTAssertThrowsError(try host.validate())
    }
    func testPanesRemainBoundedAndDisjointAcrossOddSizes() {
        for count in 1...4 {
            for width in [421.0, 800.0, 1301.0] {
                let bounds = CGRect(x: 13,y: 17,width: width,height: 611)
                let frames = SplitLayout.frames(count: count, in: bounds)
                XCTAssertEqual(frames.count, count)
                for (index, frame) in frames.enumerated() {
                    XCTAssertTrue(bounds.contains(frame))
                    XCTAssertGreaterThan(frame.width, 100); XCTAssertGreaterThan(frame.height, 100)
                    for other in frames.prefix(index) { XCTAssertFalse(frame.intersects(other)) }
                }
            }
        }
    }
}
