import XCTest
@testable import WShellCore

final class SplitTests: XCTestCase {
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
