import Foundation

public enum SplitLayout {
    public static func frames(count: Int, in bounds: CGRect, gap: CGFloat = 6) -> [CGRect] {
        let count = max(1, min(count, 4))
        if count == 1 { return [bounds] }
        let left = floor((bounds.width - gap) / 2), right = bounds.width - gap - left
        let top = floor((bounds.height - gap) / 2), bottom = bounds.height - gap - top
        let x = bounds.minX, y = bounds.minY
        if count == 2 { return [CGRect(x:x,y:y,width:left,height:bounds.height), CGRect(x:x+left+gap,y:y,width:right,height:bounds.height)] }
        if count == 3 { return [CGRect(x:x,y:y,width:left,height:bounds.height), CGRect(x:x+left+gap,y:y,width:right,height:top), CGRect(x:x+left+gap,y:y+top+gap,width:right,height:bottom)] }
        return [CGRect(x:x,y:y,width:left,height:top), CGRect(x:x+left+gap,y:y,width:right,height:top), CGRect(x:x,y:y+top+gap,width:left,height:bottom), CGRect(x:x+left+gap,y:y+top+gap,width:right,height:bottom)]
    }
}
